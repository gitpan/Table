/*
 * bltTable.c --
 *
 *	This module implements a table geometry manager for the
 *	Tk toolkit.
 *
 * Copyright 1993-1994 by AT&T Bell Laboratories.
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and warranty
 * disclaimer appear in supporting documentation, and that the
 * names of AT&T Bell Laboratories any of their entities not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * AT&T disclaims all warranties with regard to this software, including
 * all implied warranties of merchantability and fitness.  In no event
 * shall AT&T be liable for any special, indirect or consequential
 * damages or any damages whatsoever resulting from loss of use, data
 * or profits, whether in an action of contract, negligence or other
 * tortuous action, arising out of or in connection with the use or
 * performance of this software.
 *
 * Table geometry manager created by George Howlett.
 */

/*
 * To do:
 *
 * 2) No way to detect if window is already a slave of another geometry
 *    manager.
 *
 * 3) No way to detect if window is already a master of another geometry
 *    manager.  This one is bad because is can cause a bad interaction
 *    with the window manager.
 *
 * 5) Relative sizing of partitions?
 *
 * 8) Change row/column allocation procedures?
 *
 */

#include <tkPort.h>
#include <default.h>
#include <tkInt.h>
#include <tkVMacro.h>

#include "bltList.h"

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#ifndef TABLE_VERSION
#define TABLE_VERSION "1.0"
#endif

#define TRUE 	1
#define FALSE 	0

#ifndef ABS
#define ABS(x)		(((x)<0)?(-(x)):(x))
#endif /* ABS */

#ifndef SHRT_MAX
#define SHRT_MAX	0x7FFF
#endif /* SHRT_MAX */

#ifndef USHRT_MAX
#define	USHRT_MAX	0xFFFF
#endif /* USHRT_MAX */

#define NUMENTRIES(t,type) \
    (((type) == ROW_PARTITION_TYPE) ? (t)->numRows : (t)->numCols)

/*
 * The following enumerated values are used as bit flags.
 */
typedef enum {
    FILL_NONE, FILL_X, FILL_Y, FILL_BOTH
} FillFlags;

typedef enum {
    RESIZE_NONE, RESIZE_EXPAND, RESIZE_SHRINK, RESIZE_BOTH
} ResizeFlags;

static char *fillStrings[] =
{
    "none", "x", "y", "both"
};
static char *resizeStrings[] =
{
    "none", "expand", "shrink", "both"
};

/*
 * Default bounds for both partitions and slave window requested sizes.
 */
#define DEF_MAX_LIMIT	SHRT_MAX
#define DEF_MIN_LIMIT	0

#define WITHOUT_PAD 	0
#define WITH_PAD	1

/*
 * Default values for slave window attributes.
 */
#define DEF_FILL	FILL_NONE
#define DEF_IPAD_X	0
#define DEF_IPAD_Y	0
#define DEF_PAD_X	0
#define DEF_PAD_Y	0
#define DEF_COLUMN_SPAN	1
#define DEF_ROW_SPAN	1
#define DEF_ANCHOR	TK_ANCHOR_CENTER

static int initialized = 0;
static Tcl_HashTable masterWindows, slaveWindows;

/*
 * Sun's bundled and unbundled C compilers choke when using function
 * typedefs that are declared static (but can handle "extern") such as
 *
 * 	static Tk_OptionParseProc parseProc;
 *  	static Tk_OptionPrintProc printProc;
 *
 * Workaround: provide the long forward declarations here.
*/
static int ParseLimits _ANSI_ARGS_((ClientData clientData, Tcl_Interp *interp,
	Tk_Window tkwin, Arg value, char *widgRec, int offset));
static Arg PrintLimits _ANSI_ARGS_((ClientData clientData, Tk_Window tkwin,
	char *widgRec, int offset, Tcl_FreeProc **freeProcPtr));

static Tk_CustomOption LimitsOption =
{
    ParseLimits, PrintLimits, (ClientData)0
};

static int ParseFill _ANSI_ARGS_((ClientData clientData, Tcl_Interp *interp,
	Tk_Window tkwin, Arg value, char *widgRec, int offset));
static Arg PrintFill _ANSI_ARGS_((ClientData clientData, Tk_Window tkwin,
	char *widgRec, int offset, Tcl_FreeProc **freeProcPtr));

static Tk_CustomOption FillOption =
{
    ParseFill, PrintFill, (ClientData)0
};

static int ParseResize _ANSI_ARGS_((ClientData clientData, Tcl_Interp *interp,
	Tk_Window tkwin, Arg value, char *widgRec, int offset));
static Arg PrintResize _ANSI_ARGS_((ClientData clientData, Tk_Window tkwin,
	char *widgRec, int offset, Tcl_FreeProc **freeProcPtr));

static void		PrintScrollFractions _ANSI_ARGS_((int screen1,
			    int screen2, int object1, int object2,
			    double *f1, double *f2));

static Tk_CustomOption ResizeOption =
{
    ParseResize, PrintResize, (ClientData)0
};

typedef struct {
    int max, min, nom;
} Limits;

typedef enum {
    ROW_PARTITION_TYPE, COLUMN_PARTITION_TYPE
} PartitionTypes;

static char *partitionNames[] =
{
    "row", "column"
};

/*
 * Partition --
 *
 * 	A partition creates a definable space (row or column) in the
 *	table. It may have requested minimum or maximum values which
 *	constrain the size of it.
 *
 */
typedef struct {
    int size;			/* Current size of the partition. This size
				 * is bounded by minSize and maxSize. */
    int nomSize;		/* The nominal size (neither expanded nor
				 * shrunk) of the partition based upon the
				 * requested sizes of the slave windows in
				 * this partition. */
    int minSize, maxSize;	/* Size constraints on the partition */
    int offset;			/* Offset of the partition (in pixels) from
				 * the origin of the master window */
    int span;			/* Minimum spanning window in partition */

    /* user-definable fields */

    ResizeFlags resize;		/* Indicates if the partition should shrink
				 * or expand from its nominal size. */
    int pad;			/* Pads the partition beyond its nominal
				 * size */
    Limits reqSize;		/* Requested bounds for the size of the
				 * partition. The partition will not expand
				 * or shrink beyond these limits, regardless
				 * of how it was specified (max slave size).
				 * This includes any extra padding which may
				 * be specified. */
} Partition;

#define DEF_TBL_RESIZE	"both"

static Tk_ConfigSpec rowConfigSpecs[] =
{
    {TK_CONFIG_CUSTOM, "-height",          NULL,          NULL,
	         NULL, Tk_Offset(Partition, reqSize), TK_CONFIG_NULL_OK,
	&LimitsOption},
    {TK_CONFIG_PIXELS, "-pady",          NULL,          NULL,
	         NULL, Tk_Offset(Partition, pad), 0},
    {TK_CONFIG_CUSTOM, "-resize",          NULL,          NULL,
	DEF_TBL_RESIZE, Tk_Offset(Partition, resize),
	TK_CONFIG_DONT_SET_DEFAULT, &ResizeOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static Tk_ConfigSpec columnConfigSpecs[] =
{
    {TK_CONFIG_PIXELS, "-padx",          NULL,          NULL,
	         NULL, Tk_Offset(Partition, pad), 0},
    {TK_CONFIG_CUSTOM, "-resize",          NULL,          NULL,
	DEF_TBL_RESIZE, Tk_Offset(Partition, resize),
	TK_CONFIG_DONT_SET_DEFAULT, &ResizeOption},
    {TK_CONFIG_CUSTOM, "-width",          NULL,          NULL,
	         NULL, Tk_Offset(Partition, reqSize), TK_CONFIG_NULL_OK,
	&LimitsOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static Tk_ConfigSpec *partConfigSpecs[2] =
{
    rowConfigSpecs, columnConfigSpecs
};

struct Table;

/*
 * Cubicle --
 *
 * 	A cubicle is the frame which contains a slave window and its
 *	padding. It may span multiple partitions and have requested
 *	limits which constrain the size of it.  Currently, only a
 *	single window can sit in a cubicle.
 */
typedef struct {
    Tk_Window tkwin;		/* Slave window */
    struct Table *tablePtr;	/* Table managing this window */
    int x, y;			/* Last known position of the slave window
				 * in its parent window.  Used to determine
				 * if the window has moved since last
				 * layout. */
    int winwidth, winheight;
    int extBW;			/* Last known border width of slave window */


    Limits reqWidth, reqHeight;	/* Configurable bounds for width and height
				 * requests made by the slave window */
    int rowSpan;		/* Number of rows spanned by slave */
    int rowIndex;		/* Starting row of span */
    int colSpan;		/* Number of columns spanned by slave */
    int colIndex;		/* Starting column of span */

    Tk_Anchor anchor;		/* Anchor type: indicates how the window is
				 * positioned if extra space is available in
				 * the cubicle */
    int padX, padY;		/* Extra padding around the slave window */
    int ipadX, ipadY;		/* Extra padding inside of the slave window
				 * (in addition to the requested size of
				 * the window) */
    FillFlags fill;		/* Fill style flag */
    Blt_ListEntry *rowEntryPtr;	/* Pointer to cubicle in table's row sorted
				 * list */
    Blt_ListEntry *colEntryPtr;	/* Pointer to cubicle in table's column
			         * sorted list */
} Cubicle;

#define DEF_TBL_FILL	"none"
#define DEF_TBL_ANCHOR	"center"
#define DEF_TBL_SPAN	"1"

static Tk_ConfigSpec cubicleConfigSpecs[] =
{
    {TK_CONFIG_ANCHOR, "-anchor",          NULL,          NULL,
	DEF_TBL_ANCHOR, Tk_Offset(Cubicle, anchor),
	TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_INT, "-columnspan", "columnSpan",          NULL,
	DEF_TBL_SPAN, Tk_Offset(Cubicle, colSpan),
	TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-cspan", "columnSpan",          NULL,
	         NULL, Tk_Offset(Cubicle, colSpan), 0},
    {TK_CONFIG_CUSTOM, "-fill",          NULL,          NULL,
	DEF_TBL_FILL, Tk_Offset(Cubicle, fill),
	TK_CONFIG_DONT_SET_DEFAULT, &FillOption},
    {TK_CONFIG_PIXELS, "-padx",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, padX), 0},
    {TK_CONFIG_PIXELS, "-pady",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, padY), 0},
    {TK_CONFIG_PIXELS, "-ipadx",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, ipadX), 0},
    {TK_CONFIG_PIXELS, "-ipady",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, ipadY), 0},
    {TK_CONFIG_CUSTOM, "-reqheight",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, reqHeight), TK_CONFIG_NULL_OK,
	&LimitsOption},
    {TK_CONFIG_CUSTOM, "-reqwidth",          NULL,          NULL,
	         NULL, Tk_Offset(Cubicle, reqWidth), TK_CONFIG_NULL_OK,
	&LimitsOption},
    {TK_CONFIG_INT, "-rowspan", "rowSpan",          NULL,
	DEF_TBL_SPAN, Tk_Offset(Cubicle, rowSpan),
	TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-rspan", "rowSpan",          NULL,
	         NULL, Tk_Offset(Cubicle, rowSpan), 0},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * Cubicles are stored in two linked lists in the Table structure.
 * They are keyed by their starting row,column position.  The
 * following union makes a one-word key out of the row and column
 * components.  This, of course, assumes that a short int is half the
 * size of an int.  Which is NOT the case on a cray;
*/
typedef union {
    struct Position {
	unsigned short row;
	unsigned short column;
    } position;
    unsigned int index;
} SlaveKey;

/*
 * This is the default number of elements in the statically
 * pre-allocated column and row arrays.  This number should reflect a
 * useful number of row and columns, which fit most applications.
*/
#define DEF_ARRAY_SIZE	32

/*
 * Table structure
 */
typedef struct Table {
    int flags;			/* See the flags definitions below. */
    Tk_Window searchWin;	/* Main window of window hierarchy */
    Tk_Window tkwin;		/* The master window in which the slave
				 * windows are arranged. */
    Display *display;
    Tcl_Command widgetCmd;
    Tcl_Interp *interp;		/* Interpreter associated with all windows */
    Blt_LinkedList *listPtr;	/* Points to either list of row or column
				 * sorted cubicles */
    Blt_LinkedList rowSorted;	/* List of cubicles sorted by increasing
				 * order of rows spanned and row,column
				 * indices */
    Blt_LinkedList colSorted;	/* List of cubicles sorted by increasing
				 * order of columns spanned and column,row
				 * indices */
    /*
     * Pre-allocated row and column partitions.
     */
    Partition colSpace[DEF_ARRAY_SIZE];
    Partition rowSpace[DEF_ARRAY_SIZE];

    Partition *colPtr;		/* Pointer to array of column information:
				 * Initially points to colSpace */
    Partition *rowPtr;		/* Pointer to array of row information:
				 * Initially points to rowSpace */
    int rowSize;		/* Number of rows allocated */
    int numRows;		/* Number of rows currently used */
    int colSize;		/* Number of columns allocated */
    int numCols;		/* Number of columns currently used */
    int width, height;		/* Dimensions of the master window */
    int reqWidth, reqHeight;	/* Normal width and height of table */

    int borderWidth;
    Tk_3DBorder bgBorder;
    int relief;
    int highlightWidth;
    XColor *highlightBgColorPtr;
    XColor *highlightColorPtr;
    GC pixmapGC;
    int impWidth, impHeight;
    int xOrigin, yOrigin;
    LangCallback *xScrollCmd;
    LangCallback *yScrollCmd;
    int redrawX1, redrawY1, redrawX2, redrawY2;
    int scrollX1, scrollY1, scrollX2, scrollY2;
    int xScrollIncrement, yScrollIncrement;
    int inset;
    int confine;

    struct Table **Top, **Left;
} Table;

static void Tk_TableEventuallyRedraw _ANSI_ARGS_((Table *tablePtr,
						  int x1, int y1, 
						  int x2, int y2));

static int ConfigureTable _ANSI_ARGS_((Tcl_Interp *interp, Table *tablePtr,
				       int argc, Arg *args, int flags));

/*
 * Table flags definitions
 */
#define REDRAW_PENDING 1	/* A call to ArrangeTable is pending. This
				 * flag allows multiple layout changes to be
				 * requested before the table is actually
				 * reconfigured. */
#define REQUEST_LAYOUT 	2	/* Get the requested sizes of the slave
				 * windows before expanding/shrinking the
				 * size of the master window.  It's
				 * necessary to recompute the layout every
				 * time a partition or cubicle is added,
				 * reconfigured, or deleted, but not when
				 * the master window is resized. */
#define NON_PARENT	4	/* The table is managing slaves which are
				 * not children of the master window. This
				 * requires that they are moved when the
				 * master window is moved (a definite
				 * performance hit). */
#define UPDATE_SCROLLBARS 8
#define REDRAW_BORDERS 16

#define RSLBL UPDATE_SCROLLBARS|REDRAW_BORDERS

#define DEF_TABLE_X_SCROLL_CMD       ""
#define DEF_TABLE_X_SCROLL_INCREMENT "0"
#define DEF_TABLE_Y_SCROLL_CMD       ""
#define DEF_TABLE_Y_SCROLL_INCREMENT "0"
#define DEF_TABLE_IMPOS_WIDTH        "0"
#define DEF_TABLE_IMPOS_HEIGHT       "0"
#define DEF_TABLE_BG_COLOR            NORMAL_BG
#define DEF_TABLE_BG_MONO             WHITE
#define DEF_TABLE_BORDER_WIDTH                "0"
#define DEF_TABLE_CLOSE_ENOUGH                "1"
#define DEF_TABLE_CONFINE             "1"
#define DEF_TABLE_HEIGHT              "7c"
#define DEF_TABLE_HIGHLIGHT_BG                NORMAL_BG
#define DEF_TABLE_HIGHLIGHT           BLACK
#define DEF_TABLE_HIGHLIGHT_WIDTH     "1"
#define DEF_TABLE_RELIEF              "flat"

static int ParseLeftTop _ANSI_ARGS_((ClientData clientData, Tcl_Interp *interp,
	Tk_Window tkwin, Arg value, char *widgRec, int offset));
static Arg PrintLeftTop _ANSI_ARGS_((ClientData clientData, Tk_Window tkwin,
	char *widgRec, int offset, Tcl_FreeProc **freeProcPtr));

static Tk_CustomOption LeftTopOption =
{
    ParseLeftTop, PrintLeftTop, (ClientData)0
};

static Tk_ConfigSpec TableconfigSpecs[] = {
    {TK_CONFIG_BORDER, "-background", "background", "Background",
	DEF_TABLE_BG_COLOR, Tk_Offset(Table, bgBorder),
	TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
	DEF_TABLE_BG_MONO, Tk_Offset(Table, bgBorder),
	TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth",          NULL,
	         NULL, 0, 0},
    {TK_CONFIG_SYNONYM, "-bg", "background",          NULL,
	         NULL, 0, 0},
    {TK_CONFIG_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_TABLE_BORDER_WIDTH, Tk_Offset(Table, borderWidth), 0},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief",
	DEF_TABLE_RELIEF, Tk_Offset(Table, relief), 0},
    {TK_CONFIG_COLOR, "-highlightbackground", "highlightBackground",
	"HighlightBackground", DEF_TABLE_HIGHLIGHT_BG,
	Tk_Offset(Table, highlightBgColorPtr), 0},
    {TK_CONFIG_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_TABLE_HIGHLIGHT, Tk_Offset(Table, highlightColorPtr), 0},
    {TK_CONFIG_PIXELS, "-highlightthickness", "highlightThickness",
	"HighlightThickness",
	DEF_TABLE_HIGHLIGHT_WIDTH, Tk_Offset(Table, highlightWidth), 0},
    {TK_CONFIG_CALLBACK, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	DEF_TABLE_X_SCROLL_CMD, Tk_Offset(Table, xScrollCmd),
	TK_CONFIG_NULL_OK},
    {TK_CONFIG_PIXELS, "-xscrollincrement", "xScrollIncrement",
	"ScrollIncrement",
	DEF_TABLE_X_SCROLL_INCREMENT, Tk_Offset(Table, xScrollIncrement),
	0},
    {TK_CONFIG_CALLBACK, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
	DEF_TABLE_Y_SCROLL_CMD, Tk_Offset(Table, yScrollCmd),
	TK_CONFIG_NULL_OK},
    {TK_CONFIG_PIXELS, "-yscrollincrement", "yScrollIncrement",
	"ScrollIncrement",
	DEF_TABLE_Y_SCROLL_INCREMENT, Tk_Offset(Table, yScrollIncrement),
	0},
    {TK_CONFIG_PIXELS, "-height", NULL, NULL,
	DEF_TABLE_IMPOS_HEIGHT, Tk_Offset(Table, impHeight), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-width", NULL, NULL,
	DEF_TABLE_IMPOS_WIDTH, Tk_Offset(Table, impWidth), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-col",          NULL,          NULL,
	 NULL, Tk_Offset(Table, Top), TK_CONFIG_DONT_SET_DEFAULT,&LeftTopOption},
    {TK_CONFIG_CUSTOM, "-row",          NULL,          NULL,
	 NULL, Tk_Offset(Table, Left), TK_CONFIG_DONT_SET_DEFAULT,&LeftTopOption},
    {TK_CONFIG_END, (char *) NULL, (char *) NULL, (char *) NULL,
	(char *) NULL, 0, 0}
};

/*
 * Forward declarations
 */
static void ArrangeTable _ANSI_ARGS_((ClientData clientData));
static void DestroyTable _ANSI_ARGS_((ClientData clientData));
static void DestroyCubicle _ANSI_ARGS_((Cubicle *cubiPtr));
static void TableEventProc _ANSI_ARGS_((ClientData clientData,
	XEvent *eventPtr));
static void InitPartitions _ANSI_ARGS_((Partition *partPtr, int length));
static void LinkRowEntry _ANSI_ARGS_((Cubicle *cubiPtr));
static void LinkColumnEntry _ANSI_ARGS_((Cubicle *cubiPtr));

extern int strcasecmp _ANSI_ARGS_((CONST char *s1, CONST char *s2));

static void DestroyTable(clientData)
    ClientData clientData;
{
}
/*
 *----------------------------------------------------------------------
 *
 * ParseLimits --
 *
 *	Converts the list of elements into zero or more pixel values
 *	which determine the range of pixel values possible.  An element
 *	can be in any form accepted by Tk_GetPixels. The list has a
 *	different meaning based upon the number of elements.
 *
 *	    # of elements:
 *		0 - the limits are reset to the defaults.
 *		1 - the minimum and maximum values are set to this
 *		    value, freezing the range at a single value.
 *		2 - first element is the minimum, the second is the
 *		    maximum.
 *		3 - first element is the minimum, the second is the
 *		    maximum, and the third is the nominal value.
 *
 * Results:
 *	The return value is a standard Tcl result.  The min and
 *	max fields of the range are set.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ParseLimits(clientData, interp, tkwin, value, widgRec, offset)
    ClientData clientData;	/* not used */
    Tcl_Interp *interp;		/* Interpreter to send results back to */
    Tk_Window tkwin;		/* Window of table */
    Arg value;		/* New width list */
    char *widgRec;		/* Widget record */
    int offset;			/* Offset of limits */
{
    Limits *limitsPtr = (Limits *)(widgRec + offset);
    int numElem;
    Arg *elemArr = NULL;
    int result = TCL_ERROR;
    int lim[3];
    register int i;
    int size;
    LangFreeProc *freeProc = NULL;

    if (value == NULL) {
	limitsPtr->nom = limitsPtr->min = DEF_MIN_LIMIT;
	limitsPtr->max = DEF_MAX_LIMIT;
	return TCL_OK;
    }
    if (Lang_SplitList(interp, value, &numElem, &elemArr, &freeProc) != TCL_OK) {
	return TCL_ERROR;
    }
    if (numElem > 3) {
	Tcl_AppendResult(interp, "wrong # limits \"", value, "\"",
	             NULL);
	goto error;
    }
    for (i = 0; i < numElem; i++) {
	if (((LangString(elemArr[i])[0] == 'i') || (LangString(elemArr[i])[0] == 'I')) &&
	    (strcasecmp(LangString(elemArr[i]), "inf") == 0)) {
	    size = DEF_MAX_LIMIT;
	} else if (Tk_GetPixels(interp, tkwin, LangString(elemArr[i]), &size) != TCL_OK) {
	    goto error;
	}
	if ((size < DEF_MIN_LIMIT) || (size > DEF_MAX_LIMIT)) {
	    Tcl_AppendResult(interp, "invalid limit \"", value, "\"",
		         NULL);
	    goto error;
	}
	lim[i] = size;
    }
    switch (numElem) {
    case 1:
	limitsPtr->max = limitsPtr->min = lim[0];
	limitsPtr->nom = DEF_MIN_LIMIT;
	break;
    case 2:
	if (lim[1] < lim[0]) {
	    Tcl_AppendResult(interp, "invalid limits \"", value, "\"",
		         NULL);
	    goto error;
	}
	limitsPtr->min = lim[0];
	limitsPtr->max = lim[1];
	limitsPtr->nom = DEF_MIN_LIMIT;
	break;
    case 3:
	if (lim[1] < lim[0]) {
	    Tcl_AppendResult(interp, "invalid range \"", value, "\"",
		         NULL);
	    goto error;
	}
	if ((lim[2] < lim[0]) || (lim[2] > lim[1])) {
	    Tcl_AppendResult(interp, "invalid nominal \"", value, "\"",
		         NULL);
	    goto error;
	}
	limitsPtr->min = lim[0];
	limitsPtr->max = lim[1];
	limitsPtr->nom = lim[2];
	break;
    }
    result = TCL_OK;
  error:
    if (freeProc)
      (*freeProc) (numElem, elemArr);
    return result;
}


static int
LimitsToString(min, max, nom, args)
    int min, max, nom;
    Arg **args;
{
    int compte;

    if (nom > DEF_MIN_LIMIT) {
        compte = 3;
        *args = LangAllocVec(compte);
	LangSetInt((*args)+0, min);
	LangSetInt((*args)+1, max);
	LangSetInt((*args)+2, nom);
    } else if (min == max) {
        compte = 1;
        *args = LangAllocVec(compte);
	LangSetInt((*args)+0, max);
    } else if ((min != DEF_MIN_LIMIT) || (max != DEF_MAX_LIMIT)) {
        compte = 2;
        *args = LangAllocVec(compte);
	LangSetInt((*args)+0, min);
	LangSetInt((*args)+1, max);
    } else {
        compte = 0;
        *args = LangAllocVec(compte);
    }
    return compte;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintLimits --
 *
 *	Convert the limits of the pixel values allowed into a list.
 *
 * Results:
 *	The string representation of the limits is returned.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static Arg
PrintLimits(clientData, tkwin, widgRec, offset, freeProcPtr)
    ClientData clientData;	/* not used */
    Tk_Window tkwin;		/* not used */
    char *widgRec;		/* Row/column structure record */
    int offset;			/* Offset of window Partition record */
    Tcl_FreeProc **freeProcPtr;	/* Memory deallocation routine */
{
    Limits *limitsPtr = (Limits *)(widgRec + offset);
    Arg result;
    Arg *args;
    int compte;

    compte = LimitsToString(limitsPtr->min, limitsPtr->max, limitsPtr->nom, &args);
    result = Tcl_Merge(compte, args);
    LangFreeVec(compte, args);
    *freeProcPtr = (Tcl_FreeProc *)free;
    return (result);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseFill --
 *
 *	Converts the fill style string into its numeric representation.
 *	This configuration option affects how the slave window is expanded
 *	if there is extra space in the cubicle in which it sits.
 *
 *	Valid style strings are:
 *
 *		"none"	 Don't expand the window to fill the cubicle.
 * 		"x"	 Expand only the window's width.
 *		"y"	 Expand only the window's height.
 *		"both"   Expand both the window's height and width.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ParseFill(clientData, interp, tkwin, value, widgRec, offset)
    ClientData clientData;	/* not used */
    Tcl_Interp *interp;		/* Interpreter to send results back to */
    Tk_Window tkwin;		/* not used */
    Arg value;		/* Fill style string */
    char *widgRec;		/* Cubicle structure record */
    int offset;			/* Offset of style in record */
{
    FillFlags *fillPtr = (FillFlags *)(widgRec + offset);
    int length;
    char c;

    c = LangString(value)[0];
    length = strlen(LangString(value));
    if ((c == 'n') && (strncmp(LangString(value), "none", length) == 0)) {
	*fillPtr = FILL_NONE;
    } else if ((c == 'x') && (strncmp(LangString(value), "x", length) == 0)) {
	*fillPtr = FILL_X;
    } else if ((c == 'y') && (strncmp(LangString(value), "y", length) == 0)) {
	*fillPtr = FILL_Y;
    } else if ((c == 'b') && (strncmp(LangString(value), "both", length) == 0)) {
	*fillPtr = FILL_BOTH;
    } else {
	Tcl_AppendResult(interp, "bad fill argument \"", LangString(value),
	    "\": should be none, x, y, or both",          NULL);
	return TCL_ERROR;
    }
    return (TCL_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * PrintFill --
 *
 *	Returns the fill style string based upon the fill flags.
 *
 * Results:
 *	The fill style string is returned.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static Arg
PrintFill(clientData, tkwin, widgRec, offset, freeProcPtr)
    ClientData clientData;	/* not used */
    Tk_Window tkwin;		/* not used */
    char *widgRec;		/* Row/column structure record */
    int offset;			/* Offset of fill in Partition record */
    Tcl_FreeProc **freeProcPtr;	/* not used */
{
    Arg result = NULL;
    FillFlags fill = *(FillFlags *)(widgRec + offset);


    LangSetString(&result,fillStrings[(int)fill]);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseLeftTop --
 *
 *	Associate a table
 *
 *
 *----------------------------------------------------------------------
 */
static int
ParseLeftTop(clientData, interp, tkwin, value, widgRec, offset)
    ClientData clientData;	/* not used */
    Tcl_Interp *interp;		/* Interpreter to send results back to */
    Tk_Window tkwin;		/* not used */
    Arg   value;		/* pathname of the table */
    char *widgRec;		/* Cubicle structure record */
    int offset;			/* Offset of table in record */
{
    Table ***topPtr = (Table ***)(widgRec + offset);
    Table *tablePtr;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch cursor;
    int numElem;
    Arg *elemArr = NULL;
    Table **newTop;
    LangFreeProc *freeProc = NULL;
    int i;

    if (value == NULL) {
	if (*topPtr != NULL) {
	    free((char *) *topPtr);
	    *topPtr == NULL;
	}
	return TCL_OK;
    }
    if (Lang_SplitList(interp, value, &numElem, &elemArr, &freeProc) != TCL_OK) {
	return TCL_ERROR;
    }
    newTop = (Table **)LangAllocVec(numElem + 1);
    for (i = 0; i < numElem; i++) {
	for (entryPtr = Tcl_FirstHashEntry(&masterWindows, &cursor);
	     entryPtr != NULL; entryPtr = Tcl_NextHashEntry(&cursor)) {
	    tablePtr = (Table *)Tcl_GetHashValue(entryPtr);
	    if (!strcmp(Tk_PathName(tablePtr->tkwin), LangString(elemArr[i]))) {
		break;
	    }
	    tablePtr = NULL;
	}
	if (tablePtr == NULL) {
	    free((char *) newTop);
	    Tcl_AppendResult(interp, "no table associated with window \"",
			     LangString(elemArr[i]), "\"",          NULL);
	    return TCL_ERROR;
	}
	*(newTop+i) = tablePtr;
    }
    *(newTop+numElem) = (Table *)0;
    if (*topPtr != NULL) {
	free((char *) *topPtr);
    }
    *topPtr = newTop;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintLeftTop --
 *
 *	Returns pathname of an associated table
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static Arg
PrintLeftTop(clientData, tkwin, widgRec, offset, freeProcPtr)
    ClientData clientData;	/* not used */
    Tk_Window tkwin;		/* not used */
    char *widgRec;		/* Row/column structure record */
    int offset;			/* Offset of resize in Partition record */
    Tcl_FreeProc **freeProcPtr;	/* not used */
{
    Arg result = NULL;
    Table ***tablePtr = (Table ***)(widgRec + offset);
    Arg *list;
    int compte, i;
    Table **args;

    if (tablePtr != NULL) {
	for (compte = 0, args = *tablePtr; *args; args++, compte++) ;
	list = LangAllocVec(compte);
	for (i = 0, args = *tablePtr; *args; args++, i++) {
	    LangSetString(&list[i],Tk_PathName((*args)->tkwin));
	}
	result = Tcl_Merge(compte, list);
	LangFreeVec(compte, list);
	*freeProcPtr = (Tcl_FreeProc *)free;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ParseResize --
 *
 *	Converts the resize mode into its numeric representation.
 *	Valid mode strings are "none", "expand", "shrink", or "both".
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ParseResize(clientData, interp, tkwin, value, widgRec, offset)
    ClientData clientData;	/* not used */
    Tcl_Interp *interp;		/* Interpreter to send results back to */
    Tk_Window tkwin;		/* not used */
    Arg   value;		/* Resize style string */
    char *widgRec;		/* Cubicle structure record */
    int offset;			/* Offset of style in record */
{
    ResizeFlags *resizePtr = (ResizeFlags *)(widgRec + offset);
    int length;
    char c;

    c = LangString(value)[0];
    length = strlen(LangString(value));
    if ((c == 'n') && (strncmp(LangString(value), "none", length) == 0)) {
	*resizePtr = RESIZE_NONE;
    } else if ((c == 'b') && (strncmp(LangString(value), "both", length) == 0)) {
	*resizePtr = RESIZE_BOTH;
    } else if ((c == 'e') && (strncmp(LangString(value), "expand", length) == 0)) {
	*resizePtr = RESIZE_EXPAND;
    } else if ((c == 's') && (strncmp(LangString(value), "shrink", length) == 0)) {
	*resizePtr = RESIZE_SHRINK;
    } else {
	Tcl_AppendResult(interp, "bad resize argument \"", LangString(value),
	    "\": should be none, expand, shrink, or both",          NULL);
	return TCL_ERROR;
    }
    return (TCL_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * PrintResize --
 *
 *	Returns resize mode string based upon the resize flags.
 *
 * Results:
 *	The resize mode string is returned.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
static Arg
PrintResize(clientData, tkwin, widgRec, offset, freeProcPtr)
    ClientData clientData;	/* not used */
    Tk_Window tkwin;		/* not used */
    char *widgRec;		/* Row/column structure record */
    int offset;			/* Offset of resize in Partition record */
    Tcl_FreeProc **freeProcPtr;	/* not used */
{
    Arg result = NULL;
    ResizeFlags resize = *(ResizeFlags *)(widgRec + offset);

    LangSetString(&result, resizeStrings[(int)resize]);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * TableEventProc --
 *
 *	This procedure is invoked by the Tk event handler when
 *	the master window is reconfigured or destroyed.
 *
 *	The table will be rearranged at the next idle point if
 *	the master window has been resized or moved. There's a
 *	distinction made between parent and non-parent master
 *	window arrangements.  If the master window is moved and
 *	it's the parent of its slaves, the slaves are moved
 *	automatically.  If it's not the parent, the slaves need
 *	to be moved.  This can be a performance hit in rare cases
 *	where we're scrolling the master window (by moving it)
 *	and there are lots of slave windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arranges for the table associated with tkwin to have its
 *	layout re-computed and drawn at the next idle point.
 *
 *--------------------------------------------------------------
 */
static void
TableEventProc(clientData, eventPtr)
    ClientData clientData;	/* Information about window */
    XEvent *eventPtr;		/* Information about event */
{
    register Table *tablePtr = (Table *)clientData;

    if (eventPtr->type == ConfigureNotify) {
	if ((Blt_GetListLength(tablePtr->listPtr) > 0) &&
	    !(tablePtr->flags & REDRAW_PENDING)/* &&
	    ((tablePtr->width != Tk_Width(tablePtr->tkwin)) ||
		(tablePtr->height != Tk_Height(tablePtr->tkwin))
		|| (tablePtr->flags & NON_PARENT))*/) {
	    tablePtr->flags |= RSLBL;
	    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
	}
    } else if ((eventPtr->type == VisibilityNotify)  &&
	       !(tablePtr->flags & REDRAW_PENDING)) {
	tablePtr->flags |= RSLBL;
	Tk_TableEventuallyRedraw(tablePtr,
				 tablePtr->xOrigin, tablePtr->yOrigin,
				 tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
				 tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
    } else if (eventPtr->type == DestroyNotify) {
	if (tablePtr->tkwin != NULL) {
#if 0
	Tcl_DeleteHashEntry(Tcl_FindHashEntry(&masterWindows,
		(char *)tablePtr->tkwin));
#endif
	    tablePtr->tkwin = NULL;
	    Lang_DeleteWidget(tablePtr->interp,tablePtr->widgetCmd);
	}
	if (tablePtr->flags & REDRAW_PENDING) {
	    Tk_CancelIdleCall(ArrangeTable, (ClientData)tablePtr);
	}
	Tk_EventuallyFree((ClientData)tablePtr, DestroyTable);
/* j'ai un destroy table apres */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SlaveEventProc --
 *
 *	This procedure is invoked by the Tk event handler when
 *	StructureNotify events occur for a slave window.  When a slave
 *	window is destroyed, it frees the corresponding cubicle structure
 *	and arranges for the table layout to be re-computed at the next
 *	idle point.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the slave window was deleted, the Cubicle structure gets cleaned
 *	up and the table is rearranged.
 *
 *----------------------------------------------------------------------
 */

static void
SlaveEventProc(clientData, eventPtr)
    ClientData clientData;	/* Pointer to Slave structure for window
				 * referred to by eventPtr. */
    XEvent *eventPtr;		/* Describes what just happened. */
{
    Cubicle *cubiPtr = (Cubicle *)clientData;

    if (eventPtr->type == ConfigureNotify) {
	int extBW;

	extBW = Tk_Changes(cubiPtr->tkwin)->border_width;
	cubiPtr->tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
	if (!(cubiPtr->tablePtr->flags & REDRAW_PENDING) &&
	    (cubiPtr->extBW != extBW)) {
	    cubiPtr->extBW = extBW;
	    Tk_TableEventuallyRedraw((ClientData)cubiPtr->tablePtr,
	    cubiPtr->tablePtr->xOrigin, cubiPtr->tablePtr->yOrigin,
	    cubiPtr->tablePtr->xOrigin + Tk_Width(cubiPtr->tablePtr->tkwin),
	    cubiPtr->tablePtr->yOrigin + Tk_Height(cubiPtr->tablePtr->tkwin));
	}
    } else if (eventPtr->type == DestroyNotify) {
	cubiPtr->tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
	if (!(cubiPtr->tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw((ClientData)cubiPtr->tablePtr,
	    cubiPtr->tablePtr->xOrigin, cubiPtr->tablePtr->yOrigin,
	    cubiPtr->tablePtr->xOrigin + Tk_Width(cubiPtr->tablePtr->tkwin),
	    cubiPtr->tablePtr->yOrigin + Tk_Height(cubiPtr->tablePtr->tkwin));
	}
	DestroyCubicle(cubiPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * SlaveReqProc --
 *
 *	This procedure is invoked by Tk_GeometryRequest for slave
 *	windows managed by the table geometry manager.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arranges for the table associated with the slave window to
 *	have its layout re-computed and arranged at the next idle
 *	point.
 *
 *--------------------------------------------------------------
 */
/* ARGSUSED */
static void
SlaveReqProc(clientData, tkwin)
    ClientData clientData;	/* Information about window that got new
				 * preferred geometry.  */
    Tk_Window tkwin;		/* Other Tk-related information about the
			         * window. */
{
    Cubicle *cubiPtr = (Cubicle *)clientData;

    cubiPtr->tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
    if (!(cubiPtr->tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw((ClientData)cubiPtr->tablePtr,
	    cubiPtr->tablePtr->xOrigin, cubiPtr->tablePtr->yOrigin,
	    cubiPtr->tablePtr->xOrigin + Tk_Width(cubiPtr->tablePtr->tkwin),
	    cubiPtr->tablePtr->yOrigin + Tk_Height(cubiPtr->tablePtr->tkwin));
    }
}

static void
SlaveDelProc(clientData, tkwin)
    ClientData clientData;	/* Information about window that got new
				 * preferred geometry.  */
    Tk_Window tkwin;		/* Other Tk-related information about the
			         * window. */
{
    Cubicle *cubiPtr = (Cubicle *)clientData;

    if (Tk_IsMapped(cubiPtr->tkwin)) {
	Tk_UnmapWindow(cubiPtr->tkwin);
    }
    cubiPtr->tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
    if (!(cubiPtr->tablePtr->flags & REDRAW_PENDING)) {
	Tk_TableEventuallyRedraw((ClientData)cubiPtr->tablePtr,
				 cubiPtr->tablePtr->xOrigin, cubiPtr->tablePtr->yOrigin,
				 cubiPtr->tablePtr->xOrigin + Tk_Width(cubiPtr->tablePtr->tkwin),
				 cubiPtr->tablePtr->yOrigin + Tk_Height(cubiPtr->tablePtr->tkwin));
    }
    DestroyCubicle(cubiPtr);
}

static Tk_GeomMgr SlaveReq =  { "table", SlaveReqProc , SlaveDelProc };


/*
 *----------------------------------------------------------------------
 *
 * FindCubicle --
 *
 *	Searches for the Cubicle structure corresponding to the given
 *	window.
 *
 * Results:
 *	If a structure associated with the window exists, a pointer to
 *	that structure is returned. Otherwise NULL is returned and if
 *	the TCL_LEAVE_ERR_MSG flag is set, an error message is left in
 *	Tcl_GetResult(interp).
 *
 *----------------------------------------------------------------------
 */

static Cubicle *
FindCubicle(interp, tkwin, flags)
    Tcl_Interp *interp;
    Tk_Window tkwin;		/* Slave window associated with table entry */
    int flags;
{
    Tcl_HashEntry *entryPtr;

    entryPtr = Tcl_FindHashEntry(&slaveWindows, (char *)tkwin);
    if (entryPtr == NULL) {
	if (flags & TCL_LEAVE_ERR_MSG) {
	    Tcl_AppendResult(interp, "\"", Tk_PathName(tkwin),
		"\" is not managed by any table",          NULL);
	}
	return NULL;
    }
    return ((Cubicle *)Tcl_GetHashValue(entryPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * CreateCubicle --
 *
 *	This procedure creates and initializes a new Cubicle structure
 *	to contain a slave window.  A valid slave window must have a
 *	parent window that is either a) the master window or b) a mutual
 *	ancestor of the master window.
 *
 * Results:
 *	Returns a pointer to the new structure describing the new table
 *	slave window entry.  If an error occurred, then the return
 *	value is NULL and an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	Memory is allocated and initialized for the Cubicle structure.
 *
 *----------------------------------------------------------------------
 */
static Cubicle *
CreateCubicle(tablePtr, tkwin)
    Table *tablePtr;
    Tk_Window tkwin;
{
    register Cubicle *cubiPtr;
    int dummy;
    Tk_Window parent, ancestor;
    Tcl_HashEntry *entryPtr;
    int notParent = FALSE;	/* Indicates that the master window is not the
				 * parent of the new slave window. */

    /*
     * A valid slave window has a parent window that either
     *
     *    1) is the master window, or
     * 	  2) is a mutual ancestor of the master window.
     */
    ancestor = Tk_Parent(tkwin);
    for (parent = tablePtr->tkwin; (parent != ancestor) &&
	(!Tk_IsTopLevel(parent)); parent = Tk_Parent(parent)) {
	notParent = TRUE;
    }
    if (ancestor != parent) {
	Tcl_AppendResult(tablePtr->interp, "can't manage \"",
	    Tk_PathName(tkwin), "\" in table \"",
	    Tk_PathName(tablePtr->tkwin), "\"",          NULL);
	return NULL;
    }
    cubiPtr = (Cubicle *)malloc(sizeof(Cubicle));
    if (cubiPtr == NULL) {
	Tcl_SetResult(tablePtr->interp, "can't allocate cubicle",TCL_STATIC);
	return NULL;
    }
    if (notParent) {
	tablePtr->flags |= NON_PARENT;
    }
    /* Initialize the cubicle structure */

    cubiPtr->x = cubiPtr->y = cubiPtr->winwidth = cubiPtr->winheight = 0;
    cubiPtr->tkwin = tkwin;
    cubiPtr->tablePtr = tablePtr;
    cubiPtr->extBW = Tk_Changes(tkwin)->border_width;
    cubiPtr->fill = DEF_FILL;
    cubiPtr->ipadX = DEF_IPAD_X;
    cubiPtr->ipadY = DEF_IPAD_Y;
    cubiPtr->padX = DEF_PAD_X;
    cubiPtr->padY = DEF_PAD_Y;
    cubiPtr->anchor = DEF_ANCHOR;
    cubiPtr->rowSpan = DEF_ROW_SPAN;
    cubiPtr->colSpan = DEF_COLUMN_SPAN;
    cubiPtr->reqWidth.min = cubiPtr->reqHeight.min = DEF_MIN_LIMIT;
    cubiPtr->reqWidth.max = cubiPtr->reqHeight.max = DEF_MAX_LIMIT;
    cubiPtr->reqWidth.nom = cubiPtr->reqHeight.nom = DEF_MIN_LIMIT;
    cubiPtr->rowEntryPtr = cubiPtr->colEntryPtr = NULL;
    entryPtr = Tcl_CreateHashEntry(&slaveWindows, (char *)tkwin, &dummy);
    Tcl_SetHashValue(entryPtr, (char *)cubiPtr);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, SlaveEventProc,
	(ClientData)cubiPtr);
    Tk_ManageGeometry(tkwin, &SlaveReq, (ClientData)cubiPtr);
    return (cubiPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyCubicle --
 *
 *	Removes the Cubicle structure from the hash table and frees
 *	the memory allocated by it.  If the table is still in use
 *	(i.e. was not called from DestoryTable), remove its entries
 *	from the lists of row and column sorted partitions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the cubicle is freed up.
 *
 *----------------------------------------------------------------------
 */
static void
DestroyCubicle(cubiPtr)
    Cubicle *cubiPtr;
{
    Tcl_HashEntry *entryPtr;

    if (cubiPtr->rowEntryPtr != NULL) {
	Blt_DeleteListEntry(&(cubiPtr->tablePtr->rowSorted),
	    cubiPtr->rowEntryPtr);
    }
    if (cubiPtr->colEntryPtr != NULL) {
	Blt_DeleteListEntry(&(cubiPtr->tablePtr->colSorted),
	    cubiPtr->colEntryPtr);
    }
    Tk_DeleteEventHandler(cubiPtr->tkwin, StructureNotifyMask,
	SlaveEventProc, (ClientData)cubiPtr);
    Tk_ManageGeometry(cubiPtr->tkwin, (Tk_GeomMgr *) NULL,
	(ClientData)cubiPtr);
    entryPtr = Tcl_FindHashEntry(&slaveWindows, (char *)cubiPtr->tkwin);
    Tcl_DeleteHashEntry(entryPtr);
    free((char *)cubiPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureCubicle --
 *
 *	This procedure is called to process an args/argc list, plus
 *	the Tk option database, in order to configure (or reconfigure)
 *	one or more cubicles associated with a slave window which is
 *	managed by the table geometry manager.
 *
 * Note:
 *	Currently only the one slave window can be queried while many
 *	can be reconfigured at a time.
 *
 * Results:
 *	The return value is a standard Tcl result.  If TCL_ERROR is
 *	returned, then Tcl_GetResult(interp) contains an error message.
 *
 * Side effects:
 *	The table layout is recomputed and rearranged at the next
 *	idle point.
 *
 *----------------------------------------------------------------------
 */
static int
ConfigureCubicle(clientData, interp, argc, args)
    ClientData clientData;
    Tcl_Interp *interp;
    int argc;
    Arg *args;
{
    Tk_Window searchWin = (Tk_Window)clientData;
    Cubicle *cubiPtr;
    Tk_Window tkwin;
    Table *tablePtr;
    int numSlaves, numOptions;
    int oldRowSpan, oldColSpan;
    register int i;

    /* Find the number of slave windows to be configured */
    numSlaves = 0;
    for (i = 0; i < argc; i++) {
	if (LangString(args[i])[0] != '.') {
	    break;
	}
	numSlaves++;
    }
    /* And the number of options to be applied */
    numOptions = argc - numSlaves;

    for (i = 0; i < numSlaves; i++) {
	tkwin = Tk_NameToWindow(interp, LangString(args[i]), searchWin);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	cubiPtr = FindCubicle(interp, tkwin, TCL_LEAVE_ERR_MSG);
	if (cubiPtr == NULL) {
	    return TCL_ERROR;
	}
	if (numOptions == 0) {
	    return (Tk_ConfigureInfo(interp, tkwin, cubicleConfigSpecs,
		    (char *)cubiPtr,          NULL, 0));
	} else if (numOptions == 1) {
	    return (Tk_ConfigureInfo(interp, tkwin, cubicleConfigSpecs,
		    (char *)cubiPtr, LangString(args[numSlaves]), 0));
	}
	oldRowSpan = cubiPtr->rowSpan;
	oldColSpan = cubiPtr->colSpan;
	if (Tk_ConfigureWidget(interp, tkwin, cubicleConfigSpecs,
		numOptions, args + numSlaves, (char *)cubiPtr,
		TK_CONFIG_ARGV_ONLY) != TCL_OK) {
	    return TCL_ERROR;
	}
	if ((cubiPtr->colSpan < 1) || (cubiPtr->colSpan > USHRT_MAX)) {
	    Tcl_AppendResult(interp, "bad column span specified for \"",
		Tk_PathName(tkwin), "\"",          NULL);
	    return TCL_ERROR;
	}
	if ((cubiPtr->rowSpan < 1) || (cubiPtr->rowSpan > USHRT_MAX)) {
	    Tcl_AppendResult(interp, "bad row span specified for \"",
		Tk_PathName(tkwin), "\"",          NULL);
	    return TCL_ERROR;
	}
	tablePtr = cubiPtr->tablePtr;
	if (oldColSpan != cubiPtr->colSpan) {
	    Blt_UnlinkListEntry(&(tablePtr->colSorted), cubiPtr->colEntryPtr);
	    LinkColumnEntry(cubiPtr);
	}
	if (oldRowSpan != cubiPtr->rowSpan) {
	    Blt_UnlinkListEntry(&(tablePtr->rowSorted), cubiPtr->rowEntryPtr);
	    LinkRowEntry(cubiPtr);
	}
	tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
	if (!(tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
	}
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FindTable --
 *
 *	Searches for a table associated with the window given by its
 *	pathname.  This window represents the master window of the table.
 *	Errors may occur because 1) pathname does not represent a valid
 *	Tk window or 2) the window is not associated with any table
 *	as its master window.
 *
 * Results:
 *	If a table entry exists, a pointer to the Table structure is
 *	returned. Otherwise NULL is returned and if the TCL_LEAVE_ERR_MSG
 *	flag is set, an error message is left is Tcl_GetResult(interp).
 *
 *----------------------------------------------------------------------
 */
#if 0
static Table *
FindTable(interp, pathName, searchWin, flags)
    Tcl_Interp *interp;		/* Interpreter to report errors back to */
    char *pathName;		/* Path name of the master window */
    Tk_Window searchWin;	/* Main window: used to search for window
				 * associated with pathname */
    int flags;			/* If non-zero, don't reset Tcl_GetResult(interp) */
{
    Tcl_HashEntry *entryPtr;
    Tk_Window tkwin;

    tkwin = Tk_NameToWindow(interp, pathName, searchWin);
    if (tkwin == NULL) {
	if (!(flags & TCL_LEAVE_ERR_MSG)) {
	    Tcl_ResetResult(interp);
	}
	return NULL;
    }
    entryPtr = Tcl_FindHashEntry(&masterWindows, (char *)tkwin);
    if (entryPtr == NULL) {
	if (flags & TCL_LEAVE_ERR_MSG) {
	    Tcl_AppendResult(interp, "no table associated with window \"",
		pathName, "\"",          NULL);
	}
	return NULL;
    }
    return ((Table *)Tcl_GetHashValue(entryPtr));
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * CreateTable --
 *
 *	This procedure creates and initializes a new Table structure
 *	with tkwin as its master window. The internal structures
 *	associated with the table are initialized.

 * Results:
 *	Returns the pointer to the new Table structure describing the
 *	new table geometry manager.  If an error occurred, the return
 *	value will be NULL and an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	Memory is allocated and initialized, an event handler is set up
 *	to watch tkwin, etc.
 *
 *----------------------------------------------------------------------
 */
static int
TableWidgetCmd();
static void
TableCmdDeletedProc();

int 
Tk_TableCmd(clientData, interp, argc, args)
    ClientData clientData;		/* Main window associated with
				 * interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int argc;			/* Number of arguments. */
    Arg *args;		/* Argument strings. */
{
    Tk_Window tkwin = (Tk_Window) clientData;
    Table *tablePtr;
    Tk_Window new;
    
    if (argc < 2) {
	Tcl_AppendResult(interp,
	    "wrong # args: should be \"", LangString(args[0]), " options\"",          NULL);
	return TCL_ERROR;
    }
    /* Initialize structures managing all table widgets once. */
    if (!initialized) {
	Tcl_InitHashTable(&masterWindows, TCL_ONE_WORD_KEYS);
	Tcl_InitHashTable(&slaveWindows, TCL_ONE_WORD_KEYS);
	initialized = TRUE;
    }
    new = Tk_CreateWindowFromPath(interp, tkwin, LangString(args[1]),          NULL);
    if (new == NULL) {
	return TCL_ERROR;
    }
    tablePtr = (Table *)ckalloc(sizeof(Table));
    if (tablePtr != NULL) {
	int dummy;
	Tcl_HashEntry *entryPtr;

	tablePtr->tkwin = new;
	tablePtr->searchWin = new;
	tablePtr->display = Tk_Display(new);
	tablePtr->widgetCmd = Lang_CreateWidget(interp, tablePtr->tkwin, TableWidgetCmd, 
						tablePtr, TableCmdDeletedProc);
	tablePtr->interp = interp;
	tablePtr->listPtr = &(tablePtr->rowSorted);
	tablePtr->flags = 0;
	tablePtr->rowSize = tablePtr->colSize = DEF_ARRAY_SIZE;
	tablePtr->numRows = tablePtr->numCols = 0;
	tablePtr->rowPtr = tablePtr->rowSpace;

	tablePtr->impWidth = 0;
	tablePtr->impHeight = 0;
	tablePtr->xScrollCmd = NULL;
	tablePtr->yScrollCmd = NULL;
	tablePtr->scrollX1 = 0;
	tablePtr->scrollY1 = 0;
	tablePtr->scrollX2 = 0;
	tablePtr->scrollY2 = 0;
	tablePtr->redrawX1 = 0;
	tablePtr->redrawY1 = 0;
	tablePtr->redrawX2 = 0;
	tablePtr->redrawY2 = 0;
	tablePtr->xScrollIncrement = 10;
	tablePtr->yScrollIncrement = 10;
	tablePtr->bgBorder = NULL;
	tablePtr->relief = TK_RELIEF_FLAT;
	tablePtr->highlightWidth = 0;
	tablePtr->highlightBgColorPtr = NULL;
	tablePtr->highlightColorPtr = NULL;
	tablePtr->pixmapGC = None;
	tablePtr->inset = 0;
	tablePtr->confine = 1;
	tablePtr->xOrigin = 0;
	tablePtr->yOrigin = 0;

	Blt_InitLinkedList(&(tablePtr->rowSorted), TCL_ONE_WORD_KEYS);
	InitPartitions(tablePtr->rowPtr, DEF_ARRAY_SIZE);

	tablePtr->colPtr = tablePtr->colSpace;
	Blt_InitLinkedList(&(tablePtr->colSorted), TCL_ONE_WORD_KEYS);
	InitPartitions(tablePtr->colPtr, DEF_ARRAY_SIZE);

	Tk_SetClass(tablePtr->tkwin, "Table");
	Tk_CreateEventHandler(tablePtr->tkwin, 
			      StructureNotifyMask|VisibilityChangeMask,
	    TableEventProc, (ClientData)tablePtr);
	entryPtr = Tcl_CreateHashEntry(&masterWindows, (char *)tablePtr->tkwin, &dummy);
	Tcl_SetHashValue(entryPtr, (ClientData)tablePtr);
	ConfigureTable(interp, tablePtr, argc-2, args+2, 0);
	Tcl_ArgResult(interp,LangWidgetArg(interp,tablePtr->tkwin));
	return TCL_OK;
    } else {
	Tcl_AppendResult(interp, "can't create table \"", LangString(args[1]), "\"",
	             NULL);
	return TCL_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyTable --
 *
 *	This procedure is invoked by Tk_EventuallyFree or Tk_Release
 *	to clean up the Table structure at a safe time (when no-one is
 *	using it anymore).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the table geometry manager is freed up.
 *
 *----------------------------------------------------------------------
 */
static void
TableCmdDeletedProc(clientData)
    ClientData clientData;	/* Table structure */
{
    register Table *tablePtr = (Table *)clientData;
    Blt_ListEntry *entryPtr;
    Cubicle *cubiPtr;
    
    for (entryPtr = Blt_FirstListEntry(tablePtr->listPtr);
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	cubiPtr->rowEntryPtr = cubiPtr->colEntryPtr = NULL;
	DestroyCubicle(cubiPtr);
    }
    Blt_ClearList(&(tablePtr->rowSorted));
    Blt_ClearList(&(tablePtr->colSorted));

    if ((tablePtr->rowPtr != NULL) &&
	(tablePtr->rowPtr != tablePtr->rowSpace)) {
	free((char *)tablePtr->rowPtr);
    }
    if ((tablePtr->colPtr != NULL) &&
	(tablePtr->colPtr != tablePtr->colSpace)) {
	free((char *)tablePtr->colPtr);
    }
    free((char *)tablePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * InitPartitions --
 *
 *	Initializes the values of the newly created elements in
 *	the partition array.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The elements of the array of Partition structures is
 *	initialized.
 *
 *----------------------------------------------------------------------
 */
static void
InitPartitions(partPtr, length)
    register Partition *partPtr;/* Array of partitions to be initialized */
    int length;			/* Number of elements in array */
{
    register int i;

    for (i = 0; i < length; i++) {
	partPtr->resize = RESIZE_BOTH;
	partPtr->reqSize.nom = partPtr->reqSize.min =
	    partPtr->size = DEF_MIN_LIMIT;
	partPtr->reqSize.max = DEF_MAX_LIMIT;
	partPtr->nomSize = 0;
	partPtr->pad = 0;
	partPtr->span = 0;
	partPtr++;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ExtendArray --
 *
 *	Resizes the partition array to a larger size.
 *
 * Results:
 *	A pointer to the newly extended array is returned.
 *
 *----------------------------------------------------------------------
 */
static Partition *
ExtendArray(partArr, oldSize, newSize)
    Partition *partArr;		/*  */
    int oldSize, newSize;
{
    Partition *newArr;

    newArr = (Partition *)malloc(newSize * sizeof(Partition));
    if (newArr != NULL) {
	if (oldSize > 0) {
	    memcpy((char *)newArr, (char *)partArr,
		oldSize * sizeof(Partition));
	}
	InitPartitions(&(newArr[oldSize]), (int)(newSize - oldSize));
    }
    return (newArr);
}

/*
 *----------------------------------------------------------------------
 *
 * AssertColumn --
 *
 *	Checks the size of the column partitions and extends the
 *	size if a larger array is needed.
 *
 * Results:
 *	Returns 1 if the column exists.  Otherwise 0 is returned and
 *	Tcl_GetResult(interp) contains an error message.
 *
 * Side effects:
 *	The size of the column partition array may be extended and
 *	initialized.
 *
 *----------------------------------------------------------------------
 */
static int
AssertColumn(tablePtr, column)
    Table *tablePtr;
    int column;
{
    if (column >= tablePtr->colSize) {
	int newSize;
	Partition *newArr;

	if (column >= USHRT_MAX) {
	    Tcl_AppendResult(tablePtr->interp, "too many columns in \"",
		Tk_PathName(tablePtr->tkwin), "\"",          NULL);
	    return 0;
	}
	newSize = 2 * tablePtr->colSize;
	while (newSize <= column) {
	    newSize += newSize;
	}
	newArr = ExtendArray(tablePtr->colPtr, tablePtr->colSize, newSize);
	if (newArr == NULL) {
	    Tcl_AppendResult(tablePtr->interp, "can't extend columns in table",
		" \"", Tk_PathName(tablePtr->tkwin), "\": ",
		Tcl_PosixError(tablePtr->interp));
	    return 0;
	}
	if (tablePtr->colPtr != tablePtr->colSpace) {
	    free((char *)tablePtr->colPtr);
	}
	tablePtr->colPtr = newArr;
	tablePtr->colSize = newSize;
    }
    if (column >= tablePtr->numCols) {
	tablePtr->numCols = column + 1;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * AssertRow --
 *
 *	Checks the size of the row partitions and extends the
 *	size if a larger array is needed.
 *
 * Results:
 *	Returns 1 if the row exists.  Otherwise 0 is returned
 *	and Tcl_GetResult(interp) contains an error message.
 *
 * Side effects:
 *	The size of the row partition array may be extended and
 *	initialized.
 *
 *----------------------------------------------------------------------
 */
static int
AssertRow(tablePtr, row)
    Table *tablePtr;
    int row;
{
    if (row >= tablePtr->rowSize) {
	register int newSize;
	Partition *newArr;

	if (row >= USHRT_MAX) {
	    Tcl_AppendResult(tablePtr->interp, "too many rows in \"",
		Tk_PathName(tablePtr->tkwin), "\"",          NULL);
	    return 0;
	}
	newSize = 2 * tablePtr->rowSize;
	while (newSize <= row) {
	    newSize += newSize;
	}
	newArr = ExtendArray(tablePtr->rowPtr, tablePtr->rowSize, newSize);
	if (newArr == NULL) {
	    Tcl_AppendResult(tablePtr->interp, "can't extend rows in table \"",
		Tk_PathName(tablePtr->tkwin), "\": ",
		Tcl_PosixError(tablePtr->interp));
	    return 0;
	}
	if (tablePtr->rowPtr != tablePtr->rowSpace) {
	    free((char *)tablePtr->rowPtr);
	}
	tablePtr->rowPtr = newArr;
	tablePtr->rowSize = newSize;
    }
    if (row >= tablePtr->numRows) {
	tablePtr->numRows = row + 1;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseIndex --
 *
 *	Parse the entry index string and return the row and column
 *	numbers in their respective parameters.  The format of a
 *	table entry index is <row>,<column> where <row> is the row
 *	number and <column> is the column number.  Rows and columns
 *	are numbered starting from zero.
 *
 * Results:
 *	Returns a standard Tcl result.  If TCL_OK is returned, the
 *	row and column numbers are returned via rowPtr and columnPtr
 *	respectively.
 *
 *----------------------------------------------------------------------
 */
static int
ParseIndex(interp, indexStr, rowPtr, columnPtr)
    Tcl_Interp *interp;
    char *indexStr;
    int *rowPtr;
    int *columnPtr;
{
    char *columnStr, *rowStr;
    long row, column;

    rowStr = indexStr;
    columnStr = strchr(indexStr, ',');
    if (columnStr == NULL) {
	Tcl_AppendResult(interp, "invalid index \"", indexStr,
	    "\": should be \"row,column\"",          NULL);
	return TCL_ERROR;

    }
    *columnStr++ = '\0';

    if ((sscanf(indexStr, "%ld", &row) != 1) ||
	(sscanf(columnStr, "%ld", &column) != 1)) {
      *(columnStr - 1) = ',';
      return TCL_ERROR;
    }
    *(columnStr - 1) = ',';

    if ((row < 0) || (row > USHRT_MAX)) {
	Tcl_AppendResult(interp, "row index \"", rowStr,
	    "\" is out of range",          NULL);
	return TCL_ERROR;
    }
    if ((column < 0) || (column > USHRT_MAX)) {
	Tcl_AppendResult(interp, "column index \"", columnStr,
	    "\" is out of range",          NULL);
	return TCL_ERROR;
    }
    *rowPtr = (int)row;
    *columnPtr = (int)column;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeSlaveKey --
 *
 *	Creates a one word key out of the two 16 bit row and column
 *	indices.
 *
 *----------------------------------------------------------------------
 */
static unsigned int
MakeSlaveKey(row, column)
    int row;
    int column;
{
#ifndef cray
    SlaveKey key;

    key.position.row = (unsigned short)row;
    key.position.column = (unsigned short)column;
    return (key.index);
#else
    unsigned int index;

    index = (row & 0xffffffff);
    index |= ((column & 0xffffffff) << 32);
    return (index);
#endif /*cray*/
}

/*
 *----------------------------------------------------------------------
 *
 * LinkRowEntry --
 *
 *	Links new list entry into list of row-sorted cubicles.
 *	It's important to maintain this list, because the size of
 *	the row parititions is determined in order of this list.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Entry is linked into the list of row-sorted cubicles.
 *
 *----------------------------------------------------------------------
 */
static void
LinkRowEntry(newPtr)
    Cubicle *newPtr;
{
    register int delta;
    Cubicle *cubiPtr;
    Table *tablePtr;
    register Blt_ListEntry *entryPtr;

    tablePtr = newPtr->tablePtr;
    for (entryPtr = Blt_FirstListEntry(&(tablePtr->rowSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	delta = newPtr->rowSpan - cubiPtr->rowSpan;
	if (delta < 0) {
	    break;
	} else if (delta == 0) {
	    delta = newPtr->rowIndex - cubiPtr->rowIndex;
	    if (delta > 0) {
		break;
	    } else if (delta == 0) {
		delta = newPtr->colIndex - cubiPtr->colIndex;
		if (delta > 0) {
		    break;
		}
	    }
	}
    }
    if (entryPtr == NULL) {
	Blt_LinkListAfter(&(tablePtr->rowSorted), newPtr->rowEntryPtr,
	    entryPtr);
    } else {
	Blt_LinkListBefore(&(tablePtr->rowSorted), newPtr->rowEntryPtr,
	    entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * LinkColumnEntry --
 *
 *	Links new list entry into list of column-sorted cubicles.
 *	It's important to maintain this list, because the size of
 *	the column parititions is determined in order of this list.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Entry is linked into the list of column-sorted cubicles.
 *
 *----------------------------------------------------------------------
 */
static void
LinkColumnEntry(newPtr)
    Cubicle *newPtr;
{
    register int delta;
    Cubicle *cubiPtr;
    Table *tablePtr;
    register Blt_ListEntry *entryPtr;

    tablePtr = newPtr->tablePtr;
    for (entryPtr = Blt_FirstListEntry(&(tablePtr->colSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	delta = newPtr->colSpan - cubiPtr->colSpan;
	if (delta < 0) {
	    break;
	} else if (delta == 0) {
	    delta = newPtr->colIndex - cubiPtr->colIndex;
	    if (delta > 0) {
		break;
	    } else if (delta == 0) {
		delta = newPtr->rowIndex - cubiPtr->rowIndex;
		if (delta > 0) {
		    break;
		}
	    }
	}
    }
    if (entryPtr == NULL) {
	Blt_LinkListAfter(&(tablePtr->colSorted), newPtr->colEntryPtr,
	    entryPtr);
    } else {
	Blt_LinkListBefore(&(tablePtr->colSorted), newPtr->colEntryPtr,
	    entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AddWindowToTable --
 *
 *	Adds the given window as a slave window into the table at
 *	a given row and column position.  The window may already
 *	exist as a slave window in the table. If tkwin is a slave
 *	of another table, it's an error.
 *
 *	The new window is inserted into both the slave window hash
 *	table (this is used to locate the information associated with
 *	the slave window without searching each table) and cubicle
 *	lists which are sorted in order of column and row spans.
 *
 * Results:
 *	Returns a standard Tcl result.  If an error occurred, TCL_ERROR
 *	is returned and an error message is left in Tcl_GetResult(interp).
 *
 * Side Effects:
 *	The table is re-computed and arranged at the next idle point.
 *
 *----------------------------------------------------------------------
 */

static int
AddWindowToTable(tablePtr, tkwin, row, column, argc, args)
    Table *tablePtr;
    Tk_Window tkwin;
    int row, column;
    int argc;
    Arg *args;
{
    register Cubicle *cubiPtr;
    Blt_ListEntry *entryPtr;
    int result = TCL_OK;
    unsigned int key;

    cubiPtr = FindCubicle(tablePtr->interp, tkwin, 0);
    if (cubiPtr != NULL) {
	/*
	 * Make sure the window is currently being managed by this table.
	 */
	if (cubiPtr->tablePtr != tablePtr) {
	    Tcl_AppendResult(tablePtr->interp, "\"", Tk_PathName(tkwin),
		"\" is already managed by \"", Tk_PathName(cubiPtr->tkwin),
		"\"",          NULL);
	    return TCL_ERROR;
	}
	/*
	 * Remove the cubicle from both row and column lists.  It will
	 * be re-inserted into the table at the new position
	 */
	Blt_DeleteListEntry(&(tablePtr->rowSorted), cubiPtr->rowEntryPtr);
	Blt_DeleteListEntry(&(tablePtr->colSorted), cubiPtr->colEntryPtr);
    } else {
	cubiPtr = CreateCubicle(tablePtr, tkwin);
	if (cubiPtr == NULL) {
	    return TCL_ERROR;
	}
    }
    /*
     * If there's already a slave window at this position in the
     * table, unmap it and remove the cubicle.
     */
    key = MakeSlaveKey(row, column);
    entryPtr = Blt_FindListEntry(tablePtr->listPtr, (char *)key);
    if (entryPtr != NULL) {
	Cubicle *oldPtr;

	oldPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	if (Tk_IsMapped(oldPtr->tkwin)) {
	    Tk_UnmapWindow(oldPtr->tkwin);
	}
	DestroyCubicle(oldPtr);
    }
    cubiPtr->colIndex = column;
    cubiPtr->rowIndex = row;
    if (!AssertRow(tablePtr, cubiPtr->rowIndex) ||
	!AssertColumn(tablePtr, cubiPtr->colIndex)) {
	return TCL_ERROR;
    }
    if (argc > 0) {
	result = Tk_ConfigureWidget(tablePtr->interp, cubiPtr->tkwin,
	    cubicleConfigSpecs, argc, args, (char *)cubiPtr,
	    TK_CONFIG_ARGV_ONLY);
    }
    if ((cubiPtr->colSpan < 1) || (cubiPtr->rowSpan < 1)) {
	Tcl_AppendResult(tablePtr->interp, "invalid spans specified for \"",
	    Tk_PathName(tkwin), "\"",          NULL);
	DestroyCubicle(cubiPtr);
	return TCL_ERROR;
    }
    /*
     * Insert the cubicle into both the row and column layout lists
     */
    cubiPtr->rowEntryPtr = Blt_CreateListEntry((char *)key);
    Blt_SetListValue(cubiPtr->rowEntryPtr, cubiPtr);
    LinkRowEntry(cubiPtr);

    cubiPtr->colEntryPtr = Blt_CreateListEntry((char *)key);
    Blt_SetListValue(cubiPtr->colEntryPtr, cubiPtr);
    LinkColumnEntry(cubiPtr);

    if (!AssertColumn(tablePtr, cubiPtr->colIndex + cubiPtr->colSpan - 1) ||
	!AssertRow(tablePtr, cubiPtr->rowIndex + cubiPtr->rowSpan - 1)) {
	return TCL_ERROR;
    }
    return (result);
}

/*
 *----------------------------------------------------------------------
 *
 * ManageWindows --
 *
 *	Processes an args/argc list of table entries to add and
 *	configure new slave windows into the table.  A table entry
 *	consists of the window path name, table index, and optional
 *	configuration options.  The first argument in the args list
 *	is the name of the table.  If no table exists for the given
 *	window, a new one is created.
 *
 * Results:
 *	Returns a standard Tcl result.  If an error occurred, TCL_ERROR
 *	is returned and an error message is left in Tcl_GetResult(interp).
 *
 * Side Effects:
 *	Memory is allocated, a new master table is possibly created, etc.
 *	The table is re-computed and arranged at the next idle point.
 *
 *----------------------------------------------------------------------
 */
static int
ManageWindows(tablePtr, interp, argc, args)
    Table *tablePtr;		/* Table to manage new slave windows */
    Tcl_Interp *interp;		/* Interpreter to report errors back to */
    int argc;			/*  */
    Arg *args;		/* List of slave windows, indices, and
				 * options */
{
    char *savePtr;
    int row, column;
    register int i, count;
    Tk_Window tkwin;

    for (i = 0; i < argc; /*empty*/ ) {
	tkwin = Tk_NameToWindow(interp, LangString(args[i]), tablePtr->tkwin);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	if ((i + 1) == argc) {
	    Tcl_AppendResult(interp, "missing index argument for \"", LangString(args[i]),
		"\"",          NULL);
	    return TCL_ERROR;
	}
	if (ParseIndex(interp, LangString(args[i + 1]), &row, &column) != TCL_OK) {
	    return TCL_ERROR;	/* Invalid row,column index */
	}
	/*
	 * Find the end this entry's option-value pairs, first
	 * skipping over the slave window's pathname and table index
	 * arguments.
	 */
	i += 2;
	for (count = i; count < argc; count += 2) {
	    if (LangString(args[count])[0] != '-') {
		break;
	    }
	}
	if(count < argc) {
	  savePtr = LangString(args[count]);
	  LangSetString(args+count, NULL);
        }
	if (AddWindowToTable(tablePtr, tkwin, row, column, count - i,
		args + i) != TCL_OK) {
	    return TCL_ERROR;
	}
	if(count < argc) {
	    LangSetString(args+count, savePtr);
	}
	i = count;
      }
    /* If all went well, arrange for the table layout to be performed. */
    tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
    if (!(tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
    }
    Tcl_ArgResult(interp,LangWidgetArg(interp,tablePtr->tkwin));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PartitionSizes --
 *
 *
 *	Returns the sizes of the named partitions (rows or columns)
 *
 * Results:
 *	Returns a standard Tcl result.
 *
 *----------------------------------------------------------------------
 */
/* ARGSUSED */
static int
PartitionSizes(tablePtr, interp, type, indexStr)
    Table *tablePtr;
    Tcl_Interp *interp;
    PartitionTypes type;
    Arg indexStr;
{
    long index;
    Partition *partPtr;
    Arg *indexArr;
    char string[200];
    int numPartitions, maxIndex;
    int queryAll;
    int result = TCL_ERROR;
    register int i;
    LangFreeProc *freeProc = NULL;

    if (Lang_SplitList(interp, indexStr, &numPartitions, &indexArr, &freeProc) != TCL_OK) {
	return TCL_ERROR;	/* Can't split list */
    }
    maxIndex = 0;		/* Suppress compiler warning */
    if ((numPartitions == 1) &&
	(LangString(indexArr[0])[0] == 'a') && (strcmp(LangString(indexArr[0]), "all") == 0)) {
	numPartitions = NUMENTRIES(tablePtr, type);
	queryAll = TRUE;
    } else {
	maxIndex = NUMENTRIES(tablePtr, type);
	queryAll = FALSE;
    }
    partPtr = ((type == ROW_PARTITION_TYPE)
	? tablePtr->rowPtr : tablePtr->colPtr);
    for (i = 0; i < numPartitions; i++) {
	if (queryAll) {
	    index = i;
	} else {
	    if (sscanf(LangString(indexArr[i]), "%ld", &index) != 1) {
		goto error;
	    }
	    if ((index < 0) || (index >= maxIndex)) {
		Tcl_AppendResult(interp, "index \"", indexArr[i],
		    "\" is out of range",          NULL);
		goto error;
	    }
	}
	sprintf(string, "%d", partPtr[index].size);
	Tcl_AppendElement(tablePtr->interp, string);
    }
    result = TCL_OK;
  error:
    if (freeProc)
      (*freeProc) (numPartitions, indexArr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePartition --
 *
 *	This procedure is called to process an args/argc list in order
 *	to configure a row or column in the table geometry manager.
 *
 * Results:
 *	The return value is a standard Tcl result.  If TCL_ERROR is
 *	returned, then Tcl_GetResult(interp) contains an error message.
 *
 * Side effects:
 *	Partition configuration options (bounds, resize flags, etc)
 *	get set.  New partitions may be created as necessary. The table
 *	is recalculated and arranged at the next idle point.
 *
 *----------------------------------------------------------------------
 */
static int
ConfigurePartition(tablePtr, interp, type, argc, args)
    Table *tablePtr;
    Tcl_Interp *interp;
    PartitionTypes type;
    int argc;
    Arg *args;
{
    int index;
    Partition *partPtr;
    Tk_ConfigSpec *configSpecsPtr;
    Arg *indexArr;
    int numPartitions;
    int configureAll;
    int result = TCL_ERROR;
    register int i;
    LangFreeProc *freeProc = NULL;

    if (Lang_SplitList(interp, args[0], &numPartitions, &indexArr, &freeProc) != TCL_OK) {
	return TCL_ERROR;	/* Can't split list */
    }
    configSpecsPtr = partConfigSpecs[(int)type];
    configureAll = FALSE;

    partPtr = NULL;
    if ((numPartitions == 1) &&
	(LangString(indexArr[0])[0] == 'a') && (strcmp(LangString(indexArr[0]), "all") == 0)) {
	numPartitions = NUMENTRIES(tablePtr, type);
	configureAll = TRUE;
    }
    for (i = 0; i < numPartitions; i++) {
	if (configureAll) {
	    index = i;
	} else {
	    long int value;

	    if (sscanf(LangString(indexArr[i]), "%ld", &value) != 1) {
		goto error;
	    }
	    index = (int)value;
	    if ((index < 0) || (index > USHRT_MAX)) {
		Tcl_AppendResult(interp, "index \"", indexArr[i],
		    "\" is out of range",          NULL);
		goto error;
	    }
	}
	if (type == ROW_PARTITION_TYPE) {
	    if (!AssertRow(tablePtr, index)) {
		goto error;
	    }
	    partPtr = tablePtr->rowPtr + index;
	} else if (type == COLUMN_PARTITION_TYPE) {
	    if (!AssertColumn(tablePtr, index)) {
		goto error;
	    }
	    partPtr = tablePtr->colPtr + index;
	}
	if (argc == 1) {
	    if (freeProc)
	        (*freeProc) (numPartitions, indexArr);
	    return (Tk_ConfigureInfo(interp, tablePtr->tkwin, configSpecsPtr,
		    (char *)partPtr,          NULL, 0));
	} else if (argc == 2) {
	    if (freeProc)
	        (*freeProc) (numPartitions, indexArr);
	    return (Tk_ConfigureInfo(interp, tablePtr->tkwin, configSpecsPtr,
		    (char *)partPtr, LangString(args[1]), 0));
	}
	if (Tk_ConfigureWidget(interp, tablePtr->tkwin, configSpecsPtr,
		argc - 1, args + 1, (char *)partPtr,
		TK_CONFIG_ARGV_ONLY) != TCL_OK) {
	    goto error;
	}
    }
    tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
    if (!(tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
    }
    result = TCL_OK;
  error:
    if (freeProc)
      (*freeProc) (numPartitions, indexArr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ForgetWindow --
 *
 *	Processes an args/argc list of slave window names and  purges
 *	their entries from their respective tables.  The windows are
 *	unmapped and the tables are rearranged at the next idle point.
 *	Note that all the named slave windows do not need to exist in
 *	the same table.
 *
 * Results:
 *	Returns a standard Tcl result.  If an error occurred, TCL_ERROR
 *	is returned and an error message is left in Tcl_GetResult(interp).
 *
 * Side Effects:
 *	Memory is deallocated (the cubicle is destroyed), etc.
 *	The affected tables are is re-computed and arranged at the next
 *	idle point.
 *
 *----------------------------------------------------------------------
 */

static int
ForgetWindow(tablePtr, interp, argc, args)
    Table *tablePtr;
    Tcl_Interp *interp;
    int argc;
    Arg *args;
{
    Cubicle *cubiPtr;
    register int i;
    Tk_Window tkwin;

    for (i = 0; i < argc; i++) {
	tkwin = Tk_NameToWindow(interp, LangString(args[i]), tablePtr->tkwin);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	cubiPtr = FindCubicle(interp, tkwin, TCL_LEAVE_ERR_MSG);
	if (cubiPtr == NULL) {
	    return TCL_ERROR;
	}
	if (Tk_IsMapped(cubiPtr->tkwin)) {
	    Tk_UnmapWindow(cubiPtr->tkwin);
	}
	/*
	 * Arrange for the call back here because not all the named
	 * slave windows may belong to the same table.
	 */
	cubiPtr->tablePtr->flags |= REQUEST_LAYOUT|RSLBL;
	if (!(cubiPtr->tablePtr->flags & REDRAW_PENDING)) {
	    Tk_TableEventuallyRedraw((ClientData)cubiPtr->tablePtr,
	    cubiPtr->tablePtr->xOrigin, cubiPtr->tablePtr->yOrigin,
	    cubiPtr->tablePtr->xOrigin + Tk_Width(cubiPtr->tablePtr->tkwin),
	    cubiPtr->tablePtr->yOrigin + Tk_Height(cubiPtr->tablePtr->tkwin));
	}
	DestroyCubicle(cubiPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------
 *
 * TranslateAnchor --
 *
 * 	Translate the coordinates of a given bounding box based
 *	upon the anchor specified.  The anchor indicates where
 *	the given xy position is in relation to the bounding box.
 *
 *  		nw --- n --- ne
 *  		|            |     x,y ---+
 *  		w   center   e      |     |
 *  		|            |      +-----+
 *  		sw --- s --- se
 *
 * Results:
 *	The translated coordinates of the bounding box are returned.
 *
 * -----------------------------------------------------------------
 */
static XPoint
TranslateAnchor(deltaX, deltaY, anchor)
    int deltaX, deltaY;		/* Difference between outer and inner regions
				 */
    Tk_Anchor anchor;		/* Direction of the anchor */
{
    XPoint newPt;

    newPt.x = newPt.y = 0;
    switch (anchor) {
    case TK_ANCHOR_NW:		/* Upper left corner */
	break;
    case TK_ANCHOR_W:		/* Left center */
	newPt.y = (deltaY / 2);
	break;
    case TK_ANCHOR_SW:		/* Lower left corner */
	newPt.y = deltaY;
	break;
    case TK_ANCHOR_N:		/* Top center */
	newPt.x = (deltaX / 2);
	break;
    case TK_ANCHOR_CENTER:	/* Centered */
	newPt.x = (deltaX / 2);
	newPt.y = (deltaY / 2);
	break;
    case TK_ANCHOR_S:		/* Bottom center */
	newPt.x = (deltaX / 2);
	newPt.y = deltaY;
	break;
    case TK_ANCHOR_NE:		/* Upper right corner */
	newPt.x = deltaX;
	break;
    case TK_ANCHOR_E:		/* Right center */
	newPt.x = deltaX;
	newPt.y = (deltaY / 2);
	break;
    case TK_ANCHOR_SE:		/* Lower right corner */
	newPt.x = deltaX;
	newPt.y = deltaY;
	break;
    }
    return (newPt);
}

/*
 *----------------------------------------------------------------------
 *
 * GetReqWidth --
 *
 *	Returns the width requested by the slave window starting in
 *	the given cubicle.  The requested space also includes any
 *	internal padding which has been designated for this window.
 *
 *	The requested width of the window is always bounded by
 *	the limits set in cubiPtr->reqWidth.
 *
 * Results:
 *	Returns the requested width of the slave window.
 *
 *----------------------------------------------------------------------
 */
static int
GetReqWidth(cubiPtr)
    Cubicle *cubiPtr;
{
    register int width;

    if (cubiPtr->reqWidth.nom > 0) {
	width = cubiPtr->reqWidth.nom;
    } else {
	width = Tk_ReqWidth(cubiPtr->tkwin) + (2 * cubiPtr->ipadX);
    }
    if (width < cubiPtr->reqWidth.min) {
	width = cubiPtr->reqWidth.min;
    } else if (width > cubiPtr->reqWidth.max) {
	width = cubiPtr->reqWidth.max;
    }
    return (width);
}

/*
 *----------------------------------------------------------------------
 *
 * GetReqHeight --
 *
 *	Returns the height requested by the slave window starting in
 *	the given cubicle.  The requested space also includes any
 *	internal padding which has been designated for this window.
 *
 *	The requested height of the window is always bounded by
 *	the limits set in cubiPtr->reqHeight.
 *
 * Results:
 *	Returns the requested height of the slave window.
 *
 *----------------------------------------------------------------------
 */
static int
GetReqHeight(cubiPtr)
    Cubicle *cubiPtr;
{
    register int height;

    if (cubiPtr->reqHeight.nom > 0) {
	height = cubiPtr->reqHeight.nom;
    } else {
	height = Tk_ReqHeight(cubiPtr->tkwin) + (2 * cubiPtr->ipadY);
    }
    if (height < cubiPtr->reqHeight.min) {
	height = cubiPtr->reqHeight.min;
    } else if (height > cubiPtr->reqHeight.max) {
	height = cubiPtr->reqHeight.max;
    }
    return (height);
}

/*
 *----------------------------------------------------------------------
 *
 * GetSpan --
 *
 *	Calculates the distance of the given span of partitions.
 *
 * Results:
 *	Returns the space currently used in the span of partitions.
 *
 *----------------------------------------------------------------------
 */
static int
GetSpan(partArr, length, withPad)
    Partition *partArr;		/* Array of partitions */
    int length;			/* Number of partitions spanned */
    int withPad;		/* If non-zero, include the extra padding at
				 * the end partitions of the span in the space
				 * used */
{
    register Partition *partPtr;
    Partition *startPtr, *endPtr;
    register int spaceUsed;

    startPtr = partArr;
    endPtr = partArr + (length - 1);

    spaceUsed = 0;
    for (partPtr = startPtr; partPtr <= endPtr; partPtr++) {
	spaceUsed += partPtr->size;
    }
    if (!withPad) {
	spaceUsed -= (startPtr->pad + endPtr->pad);
    }
    return (spaceUsed);
}

/*
 *----------------------------------------------------------------------
 *
 * GrowSpan --
 *
 *	Expand the span by the amount of the extra space needed.
 *      This procedure is used in LayoutPartitions to grow the
 *	partitions to their minimum nominal size, starting from
 *	a zero width and height space.
 *
 *	This looks more complicated than it really is.  The idea is
 *	to make the size of the partitions correspond to the smallest
 *	cubicle spans.  For example, if window A is in column 1 and
 *	window B spans both columns 0 and 1, any extra space needed
 *	to fit window B should come from column 0.
 *
 *	On the first pass we try to add space to partitions which have
 *	not been touched yet (i.e. have no nominal size).  Since the
 *	row and column lists are sorted in ascending order of the number
 *	of rows or columns spanned, the space is distributed amongst the
 *	smallest spans first.
 *
 *	The second pass handles the case of windows which have the same
 *	span.  For example, if A and B, which span the same number of
 *	partitions are the only windows to span column 1, column 1 would
 *	grow to contain the bigger of the two slices of space.
 *
 *	If there is still extra space after the first two passes, this
 *	means that there were no partitions of with no window spans or
 *	the same order span that could be expanded. The third pass
 *	will try to remedy this by parcelling out the left over space
 *	evenly among the rest of the partitions.
 *
 *	On each pass, we have to keep iterating over the span, evenly
 *	doling out slices of extra space, because we may hit partition
 *	limits as space is donated.  In addition, if there are left
 *	over pixels because of round-off, this will distribute them as
 *	evenly as possible.  For the worst case, it will take *length*
 *	passes to expand the span.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The partitions in the span may be expanded.
 *
 *----------------------------------------------------------------------
 */
static void
GrowSpan(array, length, extraSpace)
    Partition *array;		/* Array of (column/row) partitions  */
    int length;			/* Number of partitions in the span */
    int extraSpace;		/* The amount of extra space needed to
				 * grow the span. */
{
    register Partition *partPtr;
    Partition *startPtr, *endPtr;
    int availSpace, adjustSize;
    int numAvail;		/* Number of partitions with space available */

    startPtr = array;
    endPtr = array + length;

    /*
     *-------------------------------------------------------------------
     *
     * Pass 1: Add space first to partitions which were previously empty
     *
     *-------------------------------------------------------------------
     */

    /* Find out how many partitions have no size yet */
    numAvail = 0;
    for (partPtr = startPtr; partPtr < endPtr; partPtr++) {
	if ((partPtr->nomSize == 0) && (partPtr->maxSize > partPtr->size)) {
	    numAvail++;
	}
    }
    while ((numAvail > 0) && (extraSpace > 0)) {
	adjustSize = extraSpace / numAvail;
	if (adjustSize == 0) {
	    adjustSize = 1;
	}
	for (partPtr = startPtr; (partPtr < endPtr) && (extraSpace > 0);
	    partPtr++) {
	    availSpace = partPtr->maxSize - partPtr->size;
	    if ((partPtr->nomSize == 0) && (availSpace > 0)) {
		if (adjustSize < availSpace) {
		    extraSpace -= adjustSize;
		    partPtr->size += adjustSize;
		} else {
		    extraSpace -= availSpace;
		    partPtr->size += availSpace;
		    numAvail--;
		}
		partPtr->span = length;
	    }
	}
    }

    /*
     *-------------------------------------------------------------------
     *
     * Pass 2: Add space to partitions which have the same minimum span
     *
     *-------------------------------------------------------------------
     */

    numAvail = 0;
    for (partPtr = startPtr; partPtr < endPtr; partPtr++) {
	if ((partPtr->span == length) && (partPtr->maxSize > partPtr->size)) {
	    numAvail++;
	}
    }
    while ((numAvail > 0) && (extraSpace > 0)) {
	adjustSize = extraSpace / numAvail;
	if (adjustSize == 0) {
	    adjustSize = 1;
	}
	for (partPtr = startPtr; (partPtr < endPtr) && (extraSpace > 0);
	    partPtr++) {
	    availSpace = partPtr->maxSize - partPtr->size;
	    if ((partPtr->span == length) && (availSpace > 0)) {
		if (adjustSize < availSpace) {
		    extraSpace -= adjustSize;
		    partPtr->size += adjustSize;
		} else {
		    extraSpace -= availSpace;
		    partPtr->size += availSpace;
		    numAvail--;
		}
	    }
	}
    }

    /*
     *-------------------------------------------------------------------
     *
     * Pass 3: Try to expand all the windows with space still available
     *
     *-------------------------------------------------------------------
     */

    /* Find out how many partitions still have space available */
    numAvail = 0;
    for (partPtr = startPtr; partPtr < endPtr; partPtr++) {
	if (partPtr->maxSize > partPtr->size) {
	    numAvail++;
	}
	partPtr->nomSize = partPtr->size;
    }
    while ((numAvail > 0) && (extraSpace > 0)) {
	adjustSize = extraSpace / numAvail;
	if (adjustSize == 0) {
	    adjustSize = 1;
	}
	for (partPtr = startPtr; (partPtr < endPtr) && (extraSpace > 0);
	    partPtr++) {
	    availSpace = partPtr->maxSize - partPtr->size;
	    if (availSpace > 0) {
		if (adjustSize < availSpace) {
		    extraSpace -= adjustSize;
		    partPtr->nomSize = partPtr->size =
			(partPtr->size + adjustSize);
		} else {
		    extraSpace -= availSpace;
		    partPtr->nomSize = partPtr->size =
			(partPtr->size + availSpace);
		    numAvail--;
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustPartitions --
 *
 *	Adjust the span by the amount of the extra space needed.
 *	If the amount (extraSpace) is negative, shrink the span,
 *	otherwise expand it.  Size constraints on the partitions may
 *	prevent any or all of the spacing adjustments.
 *
 *	This is very much like the GrowSpan procedure, but in this
 *	case we are shrinking or expanding all the (row or column)
 *	partitions. It uses a two pass approach, first giving
 *	space to partitions which not are smaller/larger than their
 *	nominal sizes. This is because constraints on the partitions
 *	may cause resizing to be non-linear.
 *
 *	If there is still extra space, this means that all partitions
 *	are at least to their nominal sizes.  The second pass will
 *	try to add/remove the left over space evenly among all the
 *	partitions which still have space available.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The size of the partitions in the span may be increased
 *	or decreased.
 *
 *----------------------------------------------------------------------
 */
static void
AdjustPartitions(array, length, extraSpace)
    Partition *array;		/* Array of (column/row) partitions  */
    int length;			/* Number of partitions */
    int extraSpace;		/* The amount of extra space to grow or shrink
				 * the span. If negative, it represents the
				 * amount of space to remove */
{
    register Partition *partPtr;
    Partition *startPtr, *endPtr;
    int availSpace, adjustSize;
    int numAvail;

    startPtr = array;
    endPtr = array + length;

    /*
     *-------------------------------------------------------------------
     *
     * Pass 1: Adjust partition's with space beyond its nominal size.
     *
     *-------------------------------------------------------------------
     */
    numAvail = 0;
    for (partPtr = startPtr; partPtr < endPtr; partPtr++) {
	if (extraSpace < 0) {
	    availSpace = partPtr->size - partPtr->nomSize;
	} else {
	    availSpace = partPtr->nomSize - partPtr->size;
	}
	if (availSpace > 0) {
	    numAvail++;
	}
    }
    while ((numAvail > 0) && (extraSpace != 0)) {
	adjustSize = extraSpace / numAvail;
	if (adjustSize == 0) {
	    adjustSize = (extraSpace > 0) ? 1 : -1;
	}
	for (partPtr = startPtr; (partPtr < endPtr) && (extraSpace != 0);
	    partPtr++) {
	    availSpace = partPtr->nomSize - partPtr->size;
	    if (((extraSpace > 0) && (availSpace > 0)) ||
		((extraSpace < 0) && (availSpace < 0))) {
		if (ABS(adjustSize) < ABS(availSpace)) {
		    extraSpace -= adjustSize;
		    partPtr->size += adjustSize;
		} else {
		    extraSpace -= availSpace;
		    partPtr->size += availSpace;
		    numAvail--;
		}
	    }
	}
    }

    /*
     *-------------------------------------------------------------------
     *
     * Pass 2: Adjust the partitions with space still available
     *
     *-------------------------------------------------------------------
     */

    numAvail = 0;
    for (partPtr = startPtr; partPtr < endPtr; partPtr++) {
	if (extraSpace > 0) {
	    availSpace = partPtr->maxSize - partPtr->size;
	} else {
	    availSpace = partPtr->size - partPtr->minSize;
	}
	if (availSpace > 0) {
	    numAvail++;
	}
    }
    while ((numAvail > 0) && (extraSpace != 0)) {
	adjustSize = extraSpace / numAvail;
	if (adjustSize == 0) {
	    adjustSize = (extraSpace > 0) ? 1 : -1;
	}
	for (partPtr = startPtr; (partPtr < endPtr) && (extraSpace != 0);
	    partPtr++) {
	    if (extraSpace > 0) {
		availSpace = partPtr->maxSize - partPtr->size;
	    } else {
		availSpace = partPtr->minSize - partPtr->size;
	    }
	    if (((extraSpace > 0) && (availSpace > 0)) ||
		((extraSpace < 0) && (availSpace < 0))) {
		if (ABS(adjustSize) < ABS(availSpace)) {
		    extraSpace -= adjustSize;
		    partPtr->size += adjustSize;
		} else {
		    extraSpace -= availSpace;
		    partPtr->size += availSpace;
		    numAvail--;
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ResetPartitions --
 *
 *	Sets/resets the size of each row and column partition to the
 *	minimum limit of the partition (this is usually zero). This
 *	routine gets called when new slave windows are added, deleted,
 *	or resized.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The size of each partition is re-initialized to its minimum size.
 *
 *----------------------------------------------------------------------
 */
static void
ResetPartitions(partPtr, length)
    register Partition *partPtr;
    int length;
{
    register int i;
    int size;

    for (i = 0; i < length; i++) {
	if (partPtr->reqSize.nom > 0) {
	    /*
	     * This could be done more cleanly.  Want to ensure that
	     * the requested nominal size is not overridden when
	     * determining the normal sizes.  So temporarily fix min
	     * and max to the nominal size and reset them back later.
	     */
	    partPtr->minSize = partPtr->maxSize = partPtr->size =
		partPtr->nomSize = partPtr->reqSize.nom;

	} else {
	    partPtr->minSize = partPtr->reqSize.min;
	    partPtr->maxSize = partPtr->reqSize.max;
	    size = 2 * partPtr->pad;
	    if (size < partPtr->minSize) {
		size = partPtr->minSize;
	    } else if (size > partPtr->maxSize) {
		size = partPtr->maxSize;
	    }
	    partPtr->nomSize = 0;
	    partPtr->size = size;
	}
	partPtr->span = 0;
	partPtr++;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetNominalSizes
 *
 *	Sets the normal sizes for each partition in the array.
 *	The partition size is the requested window size plus an
 *	amount of padding.  In addition, adjust the min/max bounds
 *	of the partition depending upon the resize flags (whether
 *	the partition can be expanded or shrunk from its normal
 *	size).
 *
 * Results:
 *	Returns the total space needed for the all the partitions.
 *
 * Side Effects:
 *	The nominal size of each partition in the array is set.
 *	This is used later to determine how to shrink or grow the
 *	table if the master window cannot be resized to accommodate
 *	exactly the size requirements of all the partitions.
 *
 *----------------------------------------------------------------------
 */
static int
SetNominalSizes(partPtr, numEntries)
    register Partition *partPtr;/* Row or column partition array */
    int numEntries;		/* Number of partitions in the array */
{
    register int i, size;
    int total = 0;

    for (i = 0; i < numEntries; i++) {
	/*
	 * Restore the real bounds after setting nominal size.
	 */
	partPtr->minSize = partPtr->reqSize.min;
	partPtr->maxSize = partPtr->reqSize.max;

	size = partPtr->size;
	if (size > partPtr->maxSize) {
	    size = partPtr->maxSize;
	}
	partPtr->nomSize = partPtr->size = size;
	total += partPtr->nomSize;

	/*
	 * If a partition can't be resized (to either expand or
	 * shrink), hold its respective limit at its normal size.
	 */

	if (!(partPtr->resize & RESIZE_EXPAND)) {
	    partPtr->maxSize = partPtr->nomSize;
	}
	if (!(partPtr->resize & RESIZE_SHRINK)) {
	    partPtr->minSize = partPtr->nomSize;
	}
	partPtr++;
    }
    return (total);
}

/*
 *----------------------------------------------------------------------
 *
 * LayoutPartitions --
 *
 *	Calculates the normal space requirements for both the row
 *	and column partitions.  Each slave window is added in order of
 *	the number of rows or columns spanned, which defines the space
 *	needed among in the partitions spanned.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The sum of normal sizes set here will be used as the normal
 *	size for the master window.
 *
 *----------------------------------------------------------------------
 */
static void
LayoutPartitions(tablePtr)
    Table *tablePtr;
{
    register Blt_ListEntry *entryPtr;
    register Cubicle *cubiPtr;
    Partition *rowPtr, *colPtr;
    int neededSpace, usedSpace, totalSpace;
    int twiceBW;

    twiceBW = (2 * Tk_InternalBorderWidth(tablePtr->tkwin));
    ResetPartitions(tablePtr->colPtr, tablePtr->numCols);
    for (entryPtr = Blt_FirstListEntry(&(tablePtr->colSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	neededSpace = GetReqWidth(cubiPtr) +
	    2 * (cubiPtr->padX + cubiPtr->extBW);
	if (neededSpace > 0) {
	    colPtr = tablePtr->colPtr + cubiPtr->colIndex;
	    usedSpace = GetSpan(colPtr, cubiPtr->colSpan, WITHOUT_PAD);
	    if (neededSpace > usedSpace) {
		GrowSpan(colPtr, cubiPtr->colSpan, neededSpace - usedSpace);
	    }
	}
    }
    totalSpace = SetNominalSizes(tablePtr->colPtr, tablePtr->numCols);
    tablePtr->reqWidth = totalSpace + twiceBW;

    ResetPartitions(tablePtr->rowPtr, tablePtr->numRows);
    for (entryPtr = Blt_FirstListEntry(&(tablePtr->rowSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	neededSpace = GetReqHeight(cubiPtr) +
	    2 * (cubiPtr->padY + cubiPtr->extBW);
	if (neededSpace > 0) {
	    rowPtr = tablePtr->rowPtr + cubiPtr->rowIndex;
	    usedSpace = GetSpan(rowPtr, cubiPtr->rowSpan, WITHOUT_PAD);
	    if (neededSpace > usedSpace) {
		GrowSpan(rowPtr, cubiPtr->rowSpan, neededSpace - usedSpace);
	    }
	}
    }
    totalSpace = SetNominalSizes(tablePtr->rowPtr, tablePtr->numRows);
    tablePtr->reqHeight = totalSpace + twiceBW;
}

/*
 *----------------------------------------------------------------------
 *
 * ArrangeCubicles
 *
 *	Places each slave window at its proper location.  First
 *	determines the size and position of the each window.
 *	It then considers the following:
 *
 *	  1. translation of slave window position its parent window.
 *	  2. fill style
 *	  3. anchor
 *	  4. external and internal padding
 *	  5. window size must be greater than zero
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The size of each partition is re-initialized its minimum size.
 *
 *----------------------------------------------------------------------
 */
static void UpdateCall(cubiPtr)
    Cubicle *cubiPtr;
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch cursor;
    Table *tablePtr;

    for (entryPtr = Tcl_FirstHashEntry(&masterWindows, &cursor);
       entryPtr != NULL; entryPtr = Tcl_NextHashEntry(&cursor)) {
	tablePtr = (Table *)Tcl_GetHashValue(entryPtr);
	if(tablePtr->tkwin == cubiPtr->tkwin) {
	    Tk_TableEventuallyRedraw(tablePtr, tablePtr->redrawX1,
				     tablePtr->redrawY1,
				     cubiPtr->winwidth - tablePtr->redrawX1,
				     cubiPtr->winheight - tablePtr->redrawY1);
	    break;
	}
    }
}

static void
ArrangeCubicles(tablePtr)
    Table *tablePtr;		/* Table widget structure */
{
    register Blt_ListEntry *entryPtr;
    register Cubicle *cubiPtr;
    register int width, height;
    Partition *colPtr, *rowPtr;
    register int x, y;
    int winWidth, winHeight;
    int deltaX, deltaY;
    Tk_Window parent, ancestor;
    int maxX, maxY;
    int screenX1, screenX2, screenY1, screenY2;

    maxX = tablePtr->width - tablePtr->inset;
    maxY = tablePtr->height - tablePtr->inset;
    /*
     * Compute the intersection between the area that needs redrawing
     * and the area that's visible on the screen.
     */

    if ((tablePtr->redrawX1 < tablePtr->redrawX2)
	    && (tablePtr->redrawY1 < tablePtr->redrawY2)) {
	screenX1 = tablePtr->xOrigin;
	screenY1 = tablePtr->yOrigin;
	screenX2 = tablePtr->xOrigin + maxX;
	screenY2 = tablePtr->yOrigin + maxY;
	if (tablePtr->redrawX1 > screenX1) {
	    screenX1 = tablePtr->redrawX1;
	}
	if (tablePtr->redrawY1 > screenY1) {
	    screenY1 = tablePtr->redrawY1;
	}
	if (tablePtr->redrawX2 < screenX2) {
	    screenX2 = tablePtr->redrawX2;
	}
	if (tablePtr->redrawY2 < screenY2) {
	    screenY2 = tablePtr->redrawY2;
	}
	if ((screenX1 >= screenX2) || (screenY1 >= screenY2)) {
	    goto borders;
	}
    }

    for (entryPtr = tablePtr->listPtr->tailPtr; entryPtr != NULL;
	entryPtr = entryPtr->prevPtr) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);

	colPtr = tablePtr->colPtr + cubiPtr->colIndex;
	rowPtr = tablePtr->rowPtr + cubiPtr->rowIndex;

	x = colPtr->offset + colPtr->pad + cubiPtr->padX + cubiPtr->extBW;
	y = rowPtr->offset + rowPtr->pad + cubiPtr->padY + cubiPtr->extBW;
	x -= screenX1;
	y -= screenY1;

	/*
	 * Unmap any slave windows which start outside of the master
	 * window
	 */
	width = GetSpan(colPtr, cubiPtr->colSpan, WITHOUT_PAD) -
	    (2 * (cubiPtr->padX + cubiPtr->extBW));
	height = GetSpan(rowPtr, cubiPtr->rowSpan, WITHOUT_PAD) -
	    (2 * (cubiPtr->padY + cubiPtr->extBW));

	if (((x + width) < 0) || ((y + height) < 0) || 
	    (x >= screenX2) || (y >= screenY2)) {
	    if (Tk_IsMapped(cubiPtr->tkwin)) {
		Tk_UnmapWindow(cubiPtr->tkwin);
	    }
	    continue;
	}

	winWidth = GetReqWidth(cubiPtr);
	winHeight = GetReqHeight(cubiPtr);

	/*
	 *
	 * Compare the window's requested size to the size of the
	 * span.
	 *
	 * 1) If it's bigger or if the fill flag is set, make them
	 *    the same size. Check that the new size is within the
	 *    bounds set for the window.
	 *
	 * 2) Otherwise, position the window in the space according
	 *    to its anchor.
	 *
	 */

	if ((width <= winWidth) || (cubiPtr->fill & FILL_X)) {
	    winWidth = width;
	    if (winWidth > cubiPtr->reqWidth.max) {
		winWidth = cubiPtr->reqWidth.max;
	    }
	}
	if ((height <= winHeight) || (cubiPtr->fill & FILL_Y)) {
	    winHeight = height;
	    if (winHeight > cubiPtr->reqHeight.max) {
		winHeight = cubiPtr->reqHeight.max;
	    }
	}
	/*
	 * If the window is too small to be interesting (i.e. it has
	 * only an external border) then unmap it.
	 */
	if ((winWidth < 1) || (winHeight < 1)) {
	    if (Tk_IsMapped(cubiPtr->tkwin)) {
		Tk_UnmapWindow(cubiPtr->tkwin);
	    }
	    continue;
	}
	deltaX = deltaY = 0;
	if (width > winWidth) {
	    deltaX = (width - winWidth);
	}
	if (height > winHeight) {
	    deltaY = (height - winHeight);
	}
	if ((deltaX > 0) || (deltaY > 0)) {
	    XPoint newPt;

	    newPt = TranslateAnchor(deltaX, deltaY, cubiPtr->anchor);
	    x += newPt.x, y += newPt.y;
	}
	/*
	 * Clip the slave window at the bottom and/or right edge of
	 * the master
	 */
	if ((winWidth > (maxX - x)) && (tablePtr->impWidth == 0)) {
	    winWidth = (maxX - x);
	}
	if (winHeight > (maxY - y) && (tablePtr->impHeight == 0)) {
	    winHeight = (maxY - y);
	}
	/*
	 * If the slave's parent window is not the master window,
	 * translate the master window's x and y coordinates to the
	 * coordinate system of the slave's parent.
	 */

	parent = Tk_Parent(cubiPtr->tkwin);
	for (ancestor = tablePtr->tkwin; ancestor != parent;
	    ancestor = Tk_Parent(ancestor)) {
	    x += Tk_X(ancestor) + Tk_Changes(ancestor)->border_width;
	    y += Tk_Y(ancestor) + Tk_Changes(ancestor)->border_width;
	}

	/*
	 * Resize or move the window if necessary. Save the window's x
	 * and y coordinates for reference next time.
	 */
	if ((x != cubiPtr->x) || (y != cubiPtr->y) ||
	    (winWidth != Tk_Width(cubiPtr->tkwin)) ||
	    (winHeight != Tk_Height(cubiPtr->tkwin))) {
	    Tk_MoveResizeWindow(cubiPtr->tkwin, x, y, (unsigned int)winWidth,
		(unsigned int)winHeight);
	    cubiPtr->x = x, cubiPtr->y = y;
	    cubiPtr->winwidth = winWidth, cubiPtr->winheight = winHeight;
	    UpdateCall(cubiPtr);
	}
	if (!Tk_IsMapped(cubiPtr->tkwin)) {
	    Tk_MapWindow(cubiPtr->tkwin);
	}
    }
  borders:
    return;
}

/*
 *--------------------------------------------------------------
 *
 * TableUpdateScrollbars --
 *
 *	This procedure is invoked whenever a Table has changed in
 *	a way that requires scrollbars to be redisplayed (e.g. the
 *	view in the Table has changed).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If there are scrollbars associated with the Table, then
 *	their scrolling commands are invoked to cause them to
 *	redisplay.  If errors occur, additional Tcl commands may
 *	be invoked to process the errors.
 *
 *--------------------------------------------------------------
 */

static void
TableUpdateScrollbars(tablePtr)
    Table *tablePtr;		/* Information about table. */
{
    int result;
    double f1, f2;

    tablePtr->flags &= ~UPDATE_SCROLLBARS;
    if (tablePtr->impWidth > 0 && tablePtr->xScrollCmd != NULL) {
        tablePtr->scrollX2 = tablePtr->reqWidth;
        if (tablePtr->reqWidth <= Tk_Width(tablePtr->tkwin)) {
	    PrintScrollFractions(tablePtr->xOrigin + tablePtr->inset,
				 tablePtr->xOrigin + Tk_Width(tablePtr->tkwin)
				 - tablePtr->inset,
				 tablePtr->xOrigin + tablePtr->inset,
				 tablePtr->xOrigin + Tk_Width(tablePtr->tkwin)
				 - tablePtr->inset, &f1, &f2);
	} else {
	    PrintScrollFractions(tablePtr->xOrigin + tablePtr->inset,
				 tablePtr->xOrigin + Tk_Width(tablePtr->tkwin)
				 - tablePtr->inset, tablePtr->scrollX1, 
				 tablePtr->scrollX2, &f1, &f2);
	}
	result = LangDoCallback(tablePtr->interp, tablePtr->xScrollCmd , 0, 2, " %g %g", f1, f2);
	if (result != TCL_OK) {
	    Tk_BackgroundError(tablePtr->interp);
	}
	Tcl_ResetResult(tablePtr->interp);
    }

    if (tablePtr->impHeight > 0 && tablePtr->yScrollCmd != NULL) {
        tablePtr->scrollY2 = tablePtr->reqHeight;
        if (tablePtr->reqHeight <= Tk_Height(tablePtr->tkwin)) {
	    PrintScrollFractions(tablePtr->yOrigin + tablePtr->inset,
				 tablePtr->yOrigin + Tk_Height(tablePtr->tkwin)
				 - tablePtr->inset, 
				 tablePtr->yOrigin + tablePtr->inset,
				 tablePtr->yOrigin + Tk_Height(tablePtr->tkwin)
				 - tablePtr->inset, &f1, &f2);
	} else {
  	  PrintScrollFractions(tablePtr->yOrigin + tablePtr->inset,
			       tablePtr->yOrigin + Tk_Height(tablePtr->tkwin)
			       - tablePtr->inset, tablePtr->scrollY1, 
			       tablePtr->scrollY2, &f1, &f2);
	}
	result = LangDoCallback(tablePtr->interp, tablePtr->yScrollCmd , 0, 2, " %g %g", f1, f2);
	if (result != TCL_OK) {
	    Tk_BackgroundError(tablePtr->interp);
	}
	Tcl_ResetResult(tablePtr->interp);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ArrangeTable --
 *
 *
 * Results:
 *	None.
 *
 * Side Effects:
 * 	The slave windows in the table are possibly resized and redrawn.
 *
 *----------------------------------------------------------------------
 */

static void
ForceUnique(tablePtr, Top, type)
    Table *tablePtr;
    Table **Top;
    PartitionTypes type;
{
    int num, i, redraw;
    Partition *partPtr, *partTable;
    Table **args;

    if (Top != NULL) {
	for (args = Top; *args; args++) {
	    if ((*args)->tkwin != NULL) {
		num = NUMENTRIES(tablePtr, type);
		if (num > NUMENTRIES((*args), type)) {
		    num = NUMENTRIES((*args), type);
		}
		partPtr = ((type == ROW_PARTITION_TYPE)? (*args)->rowPtr : (*args)->colPtr);
		partTable = ((type == ROW_PARTITION_TYPE)? tablePtr->rowPtr : tablePtr->colPtr);
		redraw = 0;
		for (i = 0; i < num; i++) {
		    redraw |= (partPtr->reqSize.min != partTable->size) ||
			(partPtr->reqSize.max != partTable->size);
		    partPtr->reqSize.min = partPtr->reqSize.max = partTable->size;
		    partPtr->reqSize.nom = DEF_MIN_LIMIT;
		    partPtr++;
		    partTable++;
		}
		if (redraw) {
		    (*args)->flags |= REQUEST_LAYOUT|RSLBL;
		    Tk_TableEventuallyRedraw((*args), (*args)->redrawX1, (*args)->redrawY1,
					     (*args)->redrawX2, (*args)->redrawY2);
		}
	    }
	}
    }
}

static void
ForcePartitions(tablePtr)
    Table *tablePtr;
{

    ForceUnique(tablePtr, tablePtr->Top, COLUMN_PARTITION_TYPE);
    ForceUnique(tablePtr, tablePtr->Left, ROW_PARTITION_TYPE);
}

static void
ArrangeTable(clientData)
    ClientData clientData;
{
    Table *tablePtr = (Table *)clientData;
    int width, height;
    int reqwidth, reqheight;
    register int i;
    unsigned int offset;
    int twiceBW;
    int requete;

    Tk_Preserve((ClientData)tablePtr);
    /*
     * If the table has no children anymore, then don't do anything at
     * all: just leave the master window's size as-is.
     */

    if ((Blt_GetListLength(tablePtr->listPtr) == 0) ||
	(tablePtr->tkwin == NULL)) {
	Tk_Release((ClientData)tablePtr);
	return;
    }
    if (tablePtr->flags & REQUEST_LAYOUT) {
	tablePtr->flags &= ~REQUEST_LAYOUT;
	LayoutPartitions(tablePtr);
    }
    /*
     * Initially, try to fit the partitions exactly into the master
     * window by resizing the window.  If the window's requested size
     * is different, send a request to the master window's geometry
     * manager to resize.
     */

    if ((Tk_Width(tablePtr->tkwin) <= 2 * (tablePtr->inset + 1)) || 
	(Tk_Height(tablePtr->tkwin) <= 2 * (tablePtr->inset + 1)) || 
	(Tk_Width(tablePtr->tkwin) <= tablePtr->impWidth) || 
	(Tk_Height(tablePtr->tkwin) <= tablePtr->impHeight) ||
	(tablePtr->impWidth == 0) || (tablePtr->impHeight == 0)) {

	requete = 0;

	if (Tk_Height(tablePtr->tkwin) > Tk_ReqHeight(tablePtr->tkwin)) {
	    reqheight = Tk_Height(tablePtr->tkwin);
	} else {
	    reqheight = Tk_ReqHeight(tablePtr->tkwin);
	}
	if (reqheight < tablePtr->impHeight) {
	    requete = 1;
	    reqheight = tablePtr->impHeight;
	}


	if (Tk_Width(tablePtr->tkwin) > Tk_ReqWidth(tablePtr->tkwin)) {
	    reqwidth = Tk_Width(tablePtr->tkwin);
	} else {
	    reqwidth = Tk_ReqWidth(tablePtr->tkwin);
	}
	if (reqwidth < tablePtr->impWidth) {
	    requete = 1;
	    reqwidth = tablePtr->impWidth;
	}

	if (((reqwidth <= 2 * (tablePtr->inset + 1)) || 
	      (tablePtr->impWidth == 0)) &&
	     (tablePtr->reqWidth > Tk_ReqWidth(tablePtr->tkwin))) {
	    requete = 1;
	    reqwidth = tablePtr->reqWidth;
	}
	if (((reqheight <= 2 * (tablePtr->inset + 1)) ||
	      (tablePtr->impHeight == 0)) &&
	     (tablePtr->reqHeight > Tk_ReqHeight(tablePtr->tkwin))) {
	    requete = 1;
	    reqheight = tablePtr->reqHeight;
	}

	if (requete) {
	  Tk_GeometryRequest(tablePtr->tkwin, reqwidth, reqheight);
	  tablePtr->flags |= RSLBL|REDRAW_PENDING;
	  tablePtr->redrawX1 = 0;
	  tablePtr->redrawY1 = 0;
	  tablePtr->redrawX2 = reqwidth;
	  tablePtr->redrawY2 = reqheight;
	  Tk_DoWhenIdle(ArrangeTable, (ClientData) tablePtr);
	  ForcePartitions(tablePtr);
	  Tk_Release((ClientData)tablePtr);
	  return;
	}
    }

    /*
     * If the parent isn't mapped then don't do anything more: wait
     * until it gets mapped again.  Need to get at least to here to
     * reflect size needs up the window hierarchy, but there's no
     * point in actually mapping the children.
     */

    if (!Tk_IsMapped(tablePtr->tkwin)) {
	tablePtr->flags &= ~(RSLBL|REDRAW_PENDING);
	Tk_Release((ClientData)tablePtr);
	return;
    }
    /*
     * Save the width and height of the master window so we know when
     * it has changed during ConfigureNotify events.
     */

    tablePtr->width = Tk_Width(tablePtr->tkwin);
    tablePtr->height = Tk_Height(tablePtr->tkwin);
    twiceBW = (2 * tablePtr->inset);

    width = GetSpan(tablePtr->colPtr, tablePtr->numCols, WITH_PAD) + twiceBW;
    height = GetSpan(tablePtr->rowPtr, tablePtr->numRows, WITH_PAD) + twiceBW;

    /*
     * If the previous geometry request was not fulfilled (i.e. the
     * size of the master window is different from partitions' space
     * requirements), try to adjust size of the partitions to fit the
     * window.
     */

    if (tablePtr->impWidth == 0) {
        if (tablePtr->width != width) {
	    AdjustPartitions(tablePtr->colPtr, tablePtr->numCols,
			     tablePtr->width - width);
	    width = GetSpan(tablePtr->colPtr, tablePtr->numCols, WITH_PAD) +
	      twiceBW;
	}
    }
    if(tablePtr->impHeight == 0) {
        if (tablePtr->height != height) {
	    AdjustPartitions(tablePtr->rowPtr, tablePtr->numRows,
			     tablePtr->height - height);
	    height = GetSpan(tablePtr->rowPtr, tablePtr->numRows, WITH_PAD) +
	      twiceBW;
	}
    }
    /*
     * If after adjusting the size of the partitions the space
     * required does not equal the size of the window, do one of the
     * following:
     *
     * 1) If is smaller, center the table in the window.
     * 2) If it's still bigger, clip the partitions that extend beyond
     *    the edge of the master window.
     *
     * Set the row and column offsets (including the master's internal
     * border width). To be used later when positioning the slave
     * windows.
     */

    offset = tablePtr->inset;
    if (width < tablePtr->width) {
	offset += (tablePtr->width - width) / 2;
    }
    for (i = 0; i < tablePtr->numCols; i++) {
	tablePtr->colPtr[i].offset = offset;
	offset += tablePtr->colPtr[i].size;
    }

    offset = tablePtr->inset;
    if (height < tablePtr->height) {
	offset += (tablePtr->height - height) / 2;
    }
    for (i = 0; i < tablePtr->numRows; i++) {
	tablePtr->rowPtr[i].offset = offset;
	offset += tablePtr->rowPtr[i].size;
    }

    {
	int drawableXOrigin, drawableYOrigin;
	Pixmap pixmap;

	drawableXOrigin = tablePtr->redrawX1 - 30;
	drawableYOrigin = tablePtr->redrawY1 - 30;
	pixmap = Tk_GetPixmap(Tk_Display(tablePtr->tkwin), Tk_WindowId(tablePtr->tkwin),
			      (tablePtr->redrawX2 + 30 - drawableXOrigin),
			      (tablePtr->redrawY2 + 30 - drawableYOrigin),
			      Tk_Depth(tablePtr->tkwin));


	XFillRectangle(Tk_Display(tablePtr->tkwin), pixmap, tablePtr->pixmapGC,
		       tablePtr->redrawX1 - drawableXOrigin,
		       tablePtr->redrawY1 - drawableYOrigin, 
		       (unsigned) (tablePtr->redrawX2 - tablePtr->redrawX1),
		       (unsigned) (tablePtr->redrawY2 - tablePtr->redrawY1));

	ArrangeCubicles(tablePtr);
	
	XCopyArea(Tk_Display(tablePtr->tkwin), pixmap, Tk_WindowId(tablePtr->tkwin),
		  tablePtr->pixmapGC,
		  tablePtr->redrawX1 - drawableXOrigin,
		  tablePtr->redrawY1 - drawableYOrigin,
		  (unsigned) (tablePtr->redrawX2 - tablePtr->redrawX1),
		  (unsigned) (tablePtr->redrawY2 - tablePtr->redrawY1),
		  tablePtr->redrawX1 - tablePtr->xOrigin, 
		  tablePtr->redrawY1 - tablePtr->yOrigin);
	Tk_FreePixmap(Tk_Display(tablePtr->tkwin), pixmap);
    }

    if (tablePtr->flags & REDRAW_BORDERS) {
	if (tablePtr->borderWidth > 0) {
	    Tk_Draw3DRectangle(tablePtr->tkwin, Tk_WindowId(tablePtr->tkwin),
		    tablePtr->bgBorder, tablePtr->highlightWidth,
		    tablePtr->highlightWidth,
		    tablePtr->width - 2*tablePtr->highlightWidth,
		    tablePtr->height - 2*tablePtr->highlightWidth,
		    tablePtr->borderWidth, tablePtr->relief);
	}

	if (tablePtr->highlightWidth != 0) {
	    GC gc;

	    gc = Tk_GCForColor(tablePtr->highlightBgColorPtr,
			       Tk_WindowId(tablePtr->tkwin));
	    Tk_DrawFocusHighlight(tablePtr->tkwin, gc, tablePtr->highlightWidth,
		    Tk_WindowId(tablePtr->tkwin));
	}
    }

    if (tablePtr->flags & UPDATE_SCROLLBARS) {
	TableUpdateScrollbars(tablePtr);
    }
    tablePtr->flags &= ~(REDRAW_PENDING|RSLBL);
    ForcePartitions(tablePtr);
    Tk_Release((ClientData)tablePtr);
}

/*
 *--------------------------------------------------------------
 *
 * PartitionCmd --
 *
 *	This procedure is invoked to process the Tcl command
 *	that corresponds to the table geometry manager. It handles
 *	only those commands related to the table's rows or columns.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int
PartitionCmd(tablePtr, interp, type, argc, args)
    Table *tablePtr;
    Tcl_Interp *interp;
    PartitionTypes type;
    int argc;
    Arg *args;
{
    char c;
    int length;
    int result = TCL_ERROR;
    char *partClass;

    partClass = partitionNames[(int)type];
    if (argc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", LangString(args[0]),
	    " master ", partClass, " option number ?args?",          NULL);
	return TCL_ERROR;
    }
    c = LangString(args[2])[0];
    length = strlen(LangString(args[2]));
    if ((c == 'c') && (strncmp(LangString(args[2]), "configure", length) == 0)) {
	if (argc < 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", LangString(args[0]),
		" master ", partClass, " configure index",          NULL);
	    return TCL_ERROR;
	}
	result = ConfigurePartition(tablePtr, interp, type, argc - 3, args + 3);
    } else if ((c == 'd') && (strncmp(LangString(args[2]), "dimension", length) == 0)) {
	if (argc < 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", LangString(args[0]),
		" master ", partClass, " dimension",          NULL);
	    return TCL_ERROR;
	}
	Tcl_IntResults(interp,1,0, NUMENTRIES(tablePtr, type));
	result = TCL_OK;
    } else if ((c == 's') && (strncmp(LangString(args[2]), "sizes", length) == 0)) {
	if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", LangString(args[0]),
		" master ", partClass, " sizes index",          NULL);
	    return TCL_ERROR;
	}
	result = PartitionSizes(tablePtr, interp, type, args[3]);
    } else {
	Tcl_AppendResult(interp, "unknown ", partClass, " option \"", LangString(args[3]),
	    "\": should be configure, dimension, or sizes",
	             NULL);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintScrollFractions --
 *
 *	Given the range that's visible in the window and the "100%
 *	range" for what's in the table, print a string containing
 *	the scroll fractions.  This procedure is used for both x
 *	and y scrolling.
 *
 * Results:
 *	The memory pointed to by string is modified to hold
 *	two real numbers containing the scroll fractions (between
 *	0 and 1) corresponding to the other arguments.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
PrintScrollFractions(screen1, screen2, object1, object2, value1, value2)
    int screen1;		/* Lowest coordinate visible in the window. */
    int screen2;		/* Highest coordinate visible in the window. */
    int object1;		/* Lowest coordinate in the object. */
    int object2;		/* Highest coordinate in the object. */
    double *value1;
    double *value2;
{
    double range, f1, f2;

    range = object2 - object1;
    if (range <= 0) {
	f1 = f2 = 1.0;
    } else {
	f1 = (screen1 - object1)/range;
	if (f1 < 0) {
	    f1 = 0.0;
	}
	f2 = (screen2 - object1)/range;
	if (f2 > 1.0) {
	    f2 = 1.0;
	}
	if (f2 < f1) {
	    f2 = f1;
	}
    }
    *value1 = f1;
    *value2 = f2;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_TableEventuallyRedraw --
 *
 *	Arrange for part or all of a table widget to redrawn at
 *	some convenient time in the future.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The screen will eventually be refreshed.
 *
 *--------------------------------------------------------------
 */

static void
Tk_TableEventuallyRedraw(tablePtr, x1, y1, x2, y2)
    Table *tablePtr;		/* Information about widget. */
    int x1, y1;			/* Upper left corner of area to redraw.
				 * Pixels on edge are redrawn. */
    int x2, y2;			/* Lower right corner of area to redraw.
				 * Pixels on edge are not redrawn. */
{
    if ((tablePtr->tkwin == NULL) || !Tk_IsMapped(tablePtr->tkwin)) {
	return;
    }
    if (tablePtr->flags & REDRAW_PENDING) {
	if (x1 <= tablePtr->redrawX1) {
	    tablePtr->redrawX1 = x1;
	}
	if (y1 <= tablePtr->redrawY1) {
	    tablePtr->redrawY1 = y1;
	}
	if (x2 >= tablePtr->redrawX2) {
	    tablePtr->redrawX2 = x2;
	}
	if (y2 >= tablePtr->redrawY2) {
	    tablePtr->redrawY2 = y2;
	}
    } else {
	tablePtr->redrawX1 = x1;
	tablePtr->redrawY1 = y1;
	tablePtr->redrawX2 = x2;
	tablePtr->redrawY2 = y2;
	Tk_DoWhenIdle(ArrangeTable, (ClientData) tablePtr);
	tablePtr->flags |= REDRAW_PENDING;
    }
    ForcePartitions(tablePtr);
}

/*
 *--------------------------------------------------------------
 *
 * TableSetOrigin --
 *
 *	This procedure is invoked to change the mapping between
 *	table coordinates and screen coordinates in the table
 *	window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The table will be redisplayed to reflect the change in
 *	view.  In addition, scrollbars will be updated if there
 *	are any.
 *
 *--------------------------------------------------------------
 */

static void
TableSetOrigin(tablePtr, xOrigin, yOrigin)
    Table *tablePtr;	/* Information about table. */
    int xOrigin;		/* New X origin for table (table x-coord
				 * corresponding to left edge of table
				 * window). */
    int yOrigin;		/* New Y origin for table (table y-coord
				 * corresponding to top edge of table
				 * window). */
{
    int left, right, top, bottom, delta;

    /*
     * If scroll increments have been set, round the window origin
     * to the nearest multiple of the increments.  Remember, the
     * origin is the place just inside the borders,  not the upper
     * left corner.
     */

    if (tablePtr->xScrollIncrement > 0) {
	if (xOrigin >= 0) {
	    xOrigin += tablePtr->xScrollIncrement/2;
	    xOrigin -= (xOrigin + tablePtr->inset)
		    % tablePtr->xScrollIncrement;
	} else {
	    xOrigin = (-xOrigin) + tablePtr->xScrollIncrement/2;
	    xOrigin = -(xOrigin - (xOrigin - tablePtr->inset)
		    % tablePtr->xScrollIncrement);
	}
    }
    if (tablePtr->yScrollIncrement > 0) {
	if (yOrigin >= 0) {
	    yOrigin += tablePtr->yScrollIncrement/2;
	    yOrigin -= (yOrigin + tablePtr->inset)
		    % tablePtr->yScrollIncrement;
	} else {
	    yOrigin = (-yOrigin) + tablePtr->yScrollIncrement/2;
	    yOrigin = -(yOrigin - (yOrigin - tablePtr->inset)
		    % tablePtr->yScrollIncrement);
	}
    }

    /*
     * Adjust the origin if necessary to keep as much as possible of the
     * table in the view.  The variables left, right, etc. keep track of
     * how much extra space there is on each side of the view before it
     * will stick out past the scroll region.  If one side sticks out past
     * the edge of the scroll region, adjust the view to bring that side
     * back to the edge of the scrollregion (but don't move it so much that
     * the other side sticks out now).  If scroll increments are in effect,
     * be sure to adjust only by full increments.
     */

    if (tablePtr->confine) {
	left = xOrigin + tablePtr->inset - tablePtr->scrollX1;
	right = tablePtr->scrollX2
		- (xOrigin + Tk_Width(tablePtr->tkwin) - tablePtr->inset);
	top = yOrigin + tablePtr->inset - tablePtr->scrollY1;
	bottom = tablePtr->scrollY2
		- (yOrigin + Tk_Height(tablePtr->tkwin) - tablePtr->inset);
	if ((left < 0) && (right > 0)) {
	    delta = (right > -left) ? -left : right;
	    if (tablePtr->xScrollIncrement > 0) {
		delta -= delta % tablePtr->xScrollIncrement;
	    }
	    xOrigin += delta;
	} else if ((right < 0) && (left > 0)) {
	    delta = (left > -right) ? -right : left;
	    if (tablePtr->xScrollIncrement > 0) {
		delta -= delta % tablePtr->xScrollIncrement;
	    }
	    xOrigin -= delta;
	}
	if ((top < 0) && (bottom > 0)) {
	    delta = (bottom > -top) ? -top : bottom;
	    if (tablePtr->yScrollIncrement > 0) {
		delta -= delta % tablePtr->yScrollIncrement;
	    }
	    yOrigin += delta;
	} else if ((bottom < 0) && (top > 0)) {
	    delta = (top > -bottom) ? -bottom : top;
	    if (tablePtr->yScrollIncrement > 0) {
		delta -= delta % tablePtr->yScrollIncrement;
	    }
	    yOrigin -= delta;
	}
    }

    if ((xOrigin == tablePtr->xOrigin) && (yOrigin == tablePtr->yOrigin)) {
	return;
    }

    tablePtr->xOrigin = xOrigin;
    tablePtr->yOrigin = yOrigin;
    tablePtr->flags |= RSLBL;
    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
}

static int
TableUnder(tablePtr, x, y)
    Table *tablePtr;
    int x, y;
{
    register Blt_ListEntry *entryPtr;
    register Cubicle *cubiPtr;

    for (entryPtr = Blt_FirstListEntry(&(tablePtr->rowSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	if ((cubiPtr->x >= 0) && (cubiPtr->y >= 0) &&
	    (cubiPtr->winwidth > 0) && (cubiPtr->winheight > 0)) {
	    if ((x >= cubiPtr->x) && (x <= (cubiPtr->x + cubiPtr->winwidth)) &&
		(y >= cubiPtr->y) && (y <= (cubiPtr->y + cubiPtr->winheight))) {
	      Tcl_IntResults(tablePtr->interp, 1, 1, cubiPtr->rowIndex);
	      Tcl_IntResults(tablePtr->interp, 1, 1, cubiPtr->colIndex);
	      return TCL_OK;
	    }
	}
    }
    return TCL_ERROR;
}

static int
TableGet(tablePtr, x, y)
    Table *tablePtr;
    int x, y;
{
    register Blt_ListEntry *entryPtr;
    register Cubicle *cubiPtr;

    for (entryPtr = Blt_FirstListEntry(&(tablePtr->rowSorted));
	entryPtr != NULL; entryPtr = Blt_NextListEntry(entryPtr)) {
	cubiPtr = (Cubicle *)Blt_GetListValue(entryPtr);
	if ((x >= cubiPtr->rowIndex) && (x < (cubiPtr->rowIndex + cubiPtr->rowSpan)) &&
	    (y >= cubiPtr->colIndex) && (y < (cubiPtr->colIndex + cubiPtr->colSpan))) {
	      Tcl_IntResults(tablePtr->interp, 1, 1, cubiPtr->rowIndex);
	      Tcl_IntResults(tablePtr->interp, 1, 1, cubiPtr->colIndex);
	      return TCL_OK;
	  }
    }
    return TCL_ERROR;
}


/*
 *--------------------------------------------------------------
 *
 * TableCmd --
 *
 *	This procedure is invoked to process the Tcl command
 *	that corresponds to the table geometry manager.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int
ConfigureTable(interp, tablePtr, argc, args, flags)
    Tcl_Interp *interp;		/* Used for error reporting. */
    Table *tablePtr;	/* Information about widget;  may or may
				 * not already have values for some fields. */
    int argc;			/* Number of valid entries in args. */
    Arg *args;		/* Arguments. */
    int flags;			/* Flags to pass to Tk_ConfigureWidget. */
{
    XGCValues gcValues;
    GC new;

    if (Tk_ConfigureWidget(interp, tablePtr->tkwin, TableconfigSpecs,
	    argc, args, (char *) tablePtr, flags) != TCL_OK) {
	return TCL_ERROR;
    }

    Tk_SetBackgroundFromBorder(tablePtr->tkwin, tablePtr->bgBorder);
    Tk_SetInternalBorder(tablePtr->tkwin, tablePtr->borderWidth);

    if (tablePtr->highlightWidth < 0) {
	tablePtr->highlightWidth = 0;
    }
    tablePtr->inset = tablePtr->borderWidth + tablePtr->highlightWidth;

    gcValues.function = GXcopy;
    gcValues.foreground = Tk_3DBorderColor(tablePtr->bgBorder)->pixel;
    gcValues.graphics_exposures = False;
    new = Tk_GetGC(tablePtr->tkwin,
	    GCFunction|GCForeground|GCGraphicsExposures, &gcValues);
    if (tablePtr->pixmapGC != None) {
	Tk_FreeGC(tablePtr->display, tablePtr->pixmapGC);
    }
    tablePtr->pixmapGC = new;
    Tk_GeometryRequest(tablePtr->tkwin, tablePtr->width + 2*tablePtr->inset,
	    tablePtr->height + 2*tablePtr->inset);

    /*
     * Reset the table's origin (this is a no-op unless confine
     * mode has just been turned on or the scroll region has changed).
     */

    TableSetOrigin(tablePtr, tablePtr->xOrigin, tablePtr->yOrigin);
    tablePtr->flags |= RSLBL;
    Tk_TableEventuallyRedraw(tablePtr,
	    tablePtr->xOrigin, tablePtr->yOrigin,
	    tablePtr->xOrigin + Tk_Width(tablePtr->tkwin),
	    tablePtr->yOrigin + Tk_Height(tablePtr->tkwin));
    return TCL_OK;
}


static int
TableWidgetCmd(clientData, interp, argc, args)
    ClientData clientData;
    Tcl_Interp *interp;
    int argc;
    Arg *args;
{
    char c;
    int length;
    Table *tablePtr = (Table *) clientData;
    int result;

    if (argc < 2) {
	Tcl_AppendResult(interp,
	    "wrong # args: should be \"", LangString(args[0]), " options\"",          NULL);
	return TCL_ERROR;
    }
    
    c = LangString(args[1])[0];
    length = strlen(LangString(args[1]));
    if ((c == 'c') && (strncmp(LangString(args[1]), "configure", length) == 0)
	    && (length >= 3)) {
	if (argc == 2) {
	    result = Tk_ConfigureInfo(interp, tablePtr->tkwin, TableconfigSpecs,
		    (char *) tablePtr,          NULL, 0);
	} else if (argc == 3) {
	    result = Tk_ConfigureInfo(interp, tablePtr->tkwin, TableconfigSpecs,
		    (char *) tablePtr, LangString(args[2]), 0);
	} else {
	    result = ConfigureTable(interp, tablePtr, argc-2, args+2,
		    TK_CONFIG_ARGV_ONLY);
	}
	return result;
    } else if ((c == 'l') && (strncmp(LangString(args[1]), "layout", length) == 0)) {
	tablePtr->flags |= (REQUEST_LAYOUT|REDRAW_PENDING|RSLBL);
	ArrangeTable((ClientData) tablePtr);
	return TCL_OK;
    } else if ((c == 'c') && (strncmp(LangString(args[1]), "create", length) == 0)
	    && (length >= 3)) {
	if(argc >= 3 && (LangString(args[2])[0] == '.')) {
	    return (ManageWindows(tablePtr, interp, argc - 2, args + 2));
	}
    }else if ((c == 'f') && (strncmp(LangString(args[1]), "forget", length) == 0)) {
	  if (argc < 3) {
	      Tcl_AppendResult(interp, "wrong # args: should be \"", 
			       LangString(args[0]), 
			       " forget slave ?slave ...?\"", (char *)NULL);
	      return TCL_ERROR;
	  }
	  return (ForgetWindow(tablePtr, interp, argc - 2, args + 2));
    } else if ((c == 't') && (strncmp(LangString(args[1]), "tablex", length) == 0)) {
	int x;
	
	if ((argc < 3) || (argc > 4)) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[0]), 
			     " tablex screenx\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tk_GetPixels(interp, tablePtr->tkwin, LangString(args[2]), &x) != TCL_OK) {
	    return TCL_ERROR;
	}
	x += tablePtr->xOrigin;
	Tcl_IntResults(interp, 1, 0, x);
	return TCL_OK;
    } else if ((c == 't') && (strncmp(LangString(args[1]), "tabley", length) == 0)) {
	int y;
	
	if ((argc < 3) || (argc > 4)) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[0]), 
			     " tabley screeny\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tk_GetPixels(interp, tablePtr->tkwin, LangString(args[2]), &y) != TCL_OK) {
	    return TCL_ERROR;
	}
	y += tablePtr->yOrigin;
	Tcl_IntResults(interp, 1, 0, y);
	return TCL_OK;
    } else if ((c == 'U') && (strncmp(LangString(args[1]), "Under", length) == 0)) {
	int x, y;
	if (argc < 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[0]), 
			     " under x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, args[2], &x) != TCL_OK) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[2]), 
			     " under x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, args[3], &y) != TCL_OK) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[3]), 
			     " under x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	x += tablePtr->xOrigin;
	y += tablePtr->yOrigin;
	return (TableUnder(tablePtr, x, y));
    } else if ((c == 'G') && (strncmp(LangString(args[1]), "Get", length) == 0)) {
	int x, y;
	if (argc < 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[0]), 
			     " get x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, args[2], &x) != TCL_OK) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[2]), 
			     " get x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, args[3], &y) != TCL_OK) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", 
			     LangString(args[3]), 
			     " get x y\"", (char *)NULL);
	    return TCL_ERROR;
	}
	return (TableGet(tablePtr, x, y));
    } else if ((c == 'x') && (strncmp(LangString(args[1]), "xview", length) == 0)) {
	int count, type;
	int newX = 0;
	double fraction;
	
	tablePtr->scrollX2 = tablePtr->reqWidth;
	if (argc == 2) {
	    double first, last;
	    PrintScrollFractions(tablePtr->xOrigin + tablePtr->inset,
				 tablePtr->xOrigin + Tk_Width(tablePtr->tkwin)
				 - tablePtr->inset, tablePtr->scrollX1,
				 tablePtr->scrollX2, 
				 &first, &last);
	    Tcl_DoubleResults(interp, 2, 0, first, last);
	} else {
	    if (tablePtr->reqWidth <= Tk_Width(tablePtr->tkwin))
		return TCL_OK;
	    type = Tk_GetScrollInfo(interp, argc, args, &fraction, &count);
	    switch (type) {
	    case TK_SCROLL_ERROR:
	      return TCL_ERROR;
	    case TK_SCROLL_MOVETO:
	      newX = tablePtr->scrollX1 - tablePtr->inset + fraction
		* (tablePtr->scrollX2 - tablePtr->scrollX1);
	      break;
	    case TK_SCROLL_PAGES:
	      newX = tablePtr->xOrigin + count * .9
		* (Tk_Width(tablePtr->tkwin) - 2*tablePtr->inset);
	      break;
	    case TK_SCROLL_UNITS:
	      if (tablePtr->xScrollIncrement > 0) {
		newX = tablePtr->xOrigin
		  + count*tablePtr->xScrollIncrement;
	      } else {
		newX = tablePtr->xOrigin + count * .1
		  * (Tk_Width(tablePtr->tkwin)
		     - 2*tablePtr->inset);
	      }
	      break;
	    }
	    TableSetOrigin(tablePtr, newX, tablePtr->yOrigin);
	    return TCL_OK;
	}
    } else if ((c == 'y') && (strncmp(LangString(args[1]), "yview", length) == 0)) {
	int count, type;
	int newY = 0;           /* Initialization needed only to prevent
				 * gcc warnings. */
	double fraction;
	
	tablePtr->scrollY2 = tablePtr->reqHeight;
	if (argc == 2) {
	    double first, last;
	    PrintScrollFractions(tablePtr->yOrigin + tablePtr->inset,
				 tablePtr->yOrigin + Tk_Height(tablePtr->tkwin)
				 - tablePtr->inset, tablePtr->scrollY1,
				 tablePtr->scrollY2, 
				 &first, &last);
	    Tcl_DoubleResults(interp, 2, 0, first, last);
	} else {
	    if (tablePtr->reqHeight <= Tk_Height(tablePtr->tkwin))
		return TCL_OK;
	    type = Tk_GetScrollInfo(interp, argc, args, &fraction, &count);
	    switch (type) {
	    case TK_SCROLL_ERROR:
		return TCL_ERROR;
	    case TK_SCROLL_MOVETO:
		newY = tablePtr->scrollY1 - tablePtr->inset + fraction
		    * (tablePtr->scrollY2 - tablePtr->scrollY1);
		break;
	    case TK_SCROLL_PAGES:
		newY = tablePtr->yOrigin + count * .9
		    * (Tk_Height(tablePtr->tkwin)
		       - 2*tablePtr->inset);
		break;
	    case TK_SCROLL_UNITS:
		if (tablePtr->yScrollIncrement > 0) {
		    newY = tablePtr->yOrigin
			+ count*tablePtr->yScrollIncrement;
		} else {
		    newY = tablePtr->yOrigin + count * .1
			* (Tk_Height(tablePtr->tkwin)
			   - 2*tablePtr->inset);
		}
		break;
	    }
	    TableSetOrigin(tablePtr, tablePtr->xOrigin, newY);
	    return TCL_OK;
	}
    } else if ((c == 'c') && (strncmp(LangString(args[1]), "cell", length) == 0)) {
	c = LangString(args[2])[0];
	length = strlen(LangString(args[2]));
	if ((c == 'c') && (length > 2) &&	
	    (strncmp(LangString(args[2]), "configure", length) == 0)) {
	    if (argc < 3) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", 
				 LangString(args[0]),
				 " configure slave ?option-values ...?\"", 
				 (char *)NULL);
		return TCL_ERROR;
	    }
	    return (ConfigureCubicle(tablePtr->tkwin, interp, argc - 3, args + 3));
	}
    } else if ((c == 'r') && (strncmp(LangString(args[1]), "row", length) == 0)) {
	return (PartitionCmd(tablePtr, interp, ROW_PARTITION_TYPE, argc, args));
    } else if ((c == 'c') && (strncmp(LangString(args[1]), "column", length) == 0)) {
	return (PartitionCmd(tablePtr, interp, COLUMN_PARTITION_TYPE, argc, args));
    }

    Tcl_AppendResult(interp, "unknown option \"", LangString(args[1]), "\": should be\
 configure, column, forget, masters, row, or slaves",          NULL);
    return TCL_ERROR;
}

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
 
#include <tkGlue.def>
 
#include <tkPort.h>
#include <tkInt.h>
#include <tkVMacro.h>
#include <tkGlue.h>
#include <tkGlue.m>
typedef struct Command {
    Tcl_HashEntry *hPtr;        /* Pointer to the hash table entry in
                                 * interp->commandTable that refers to
                                 * this command.  Used to get a command's
                                 * name from its Tcl_Command handle. */
    Tcl_CmdProc *proc;          /* Procedure to process command. */
    ClientData clientData;      /* Arbitrary value to pass to proc. */
    Tcl_CmdDeleteProc *deleteProc;
                                /* Procedure to invoke when deleting
                                 * command. */
    ClientData deleteData;      /* Arbitrary value to pass to deleteProc
                                 * (usually the same as clientData). */
} Command;

EXTERN int              Tk_TableCmd _ANSI_ARGS_((ClientData clientData,
                            Tcl_Interp *interp, int argc, Arg *args));
 
DECLARE_VTABLES;
 
MODULE = Tk::BLT::Table       PACKAGE = Tk::BLT::Table

PROTOTYPES: DISABLE
 
BOOT:
 {
  IMPORT_VTABLES;

  Lang_TkCommand("table",Tk_TableCmd);
  EnterWidgetMethods("BLT::Table","create","tablex","tabley","Under","xview","yview","cell","row","column","forget","Get","layout",NULL);

 
 }


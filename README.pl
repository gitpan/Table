
 perl/Tk version of BLT Table is available at :

 ftp://moulon.inra.fr/pub/pTk/Table-1.03.tar.gz

 Demo : a.pl
 Docs : Table.html
        Table.pod

Installation:
    * untar it at the same level than Tk402.002, i.e.
        ./Table-1.03
        ./Tk402.002
        ./Tk402.002/pTk
       [...]

    or change 'use lib ...' and $dirTk

    * perl Makefile.PL
    * make


Modifications :

 * for compatibility with Tk-b9, name of the module is now
   Tk::BLT::Table 

 * add $table->location 

 * Thanks to Martin Andrews <andrewm@ccfadm.eeg.ccf.org> for many patches.


Guy Decoux

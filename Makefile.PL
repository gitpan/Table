use ExtUtils::MakeMaker qw(%att &WriteMakefile);

$Version = '1.01';

$inc    = "";

# If $dirTk is not defined, try to find it
 
unless (defined $dirTk) {
  if (-r "../Tk/tkGlue.h") {$dirTk = "../Tk";}
  elsif (-r "Tk/tkGlue.h") {$dirTk = "Tk" }
  elsif (-r "../tkGlue.h") {$dirTk = ".." }
  die "Please define \$dirTk in Makefile.PL, cannot find it automatically.\n"
    unless defined $dirTk;
}

require "$dirTk/myConfig.out";
require "$dirTk/MMutil";


# The existence of the ./*/Makefile.PL file causes MakeMaker
# to automatically include Makefile code for the targets
#	config, all, clean, realclean and */Makefile
# which perform the corresponding action in the subdirectory.
                           
WriteMakefile(
    'NAME'     => 'Tk::Table',
    'VERSION'  => $Version,
    'dynamic_lib' => { OTHERLDFLAGS => "$dirTk/pTk/libpTk.a"},
    'OBJECT'  => '$(O_FILES)',
    'INC'     => "-I$dirTk/pTk -I$dirTk $inc",
    'PM'      => { 'Table.pm' => '$(INST_LIBDIR)/Table.pm',
		   'Table.pod' => '$(INST_LIBDIR)/Table.pod',
		   'Table.html' => '$(INST_LIBDIR)/Table.html'},
    'SKIP'	=> [qw(distclean)]
);

sub MY::post_constants {
 my @m;
 push @m, "
.SUFFIXES:
.SUFFIXES:     .a .o .c .xs
";
 push @m, &upgrade_pic();
 join "", @m;
}




sub MY::postamble {
join('',`cat *.d`) . "
Makefile : $dirTk/myConfig.out
\$(INST_DYNAMIC): $dirTk/pTk/libpTk.a


";
}



BEGIN {
  $IsWin32   = $^O eq 'MSWin32';
  $win_arch = ($IsWin32) ? $^O : 'x'; 
  use lib ($Tk::MMutil::dir=getcwd);
}
use lib '../Tk402.002';
use Tk::MMutil;

$dir = '../Tk402.002';

Tk::MMutil::TkExtMakefile(
    'NAME'     => 'Tk::BLT::Table',
    'VERSION'  => $Version,
    'dynamic_lib' => { OTHERLDFLAGS => "$dir/pTk/libpTk.a"},
    'OBJECT'  => '$(O_FILES)',
    'INC'     => "-I$dir/pTk -I$dir $inc",
    'PM'      => { 'Table.pm' => '$(INST_LIBDIR)/Table.pm',
		   'Table.pod' => '$(INST_LIBDIR)/Table.pod',
		   'Table.html' => '$(INST_LIBDIR)/Table.html',
		   'screenshot/table_1.gif' => '$(INST_LIBDIR)/screenshot/table_1.gif',
		   'screenshot/table_2.gif' => '$(INST_LIBDIR)/screenshot/table_2.gif'},
    'SKIP'	=> [qw(distclean)]
);


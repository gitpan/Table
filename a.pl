#!/usr/local/bin/perl
BEGIN { unshift (@INC,qw(./blib .)) }
use Tk;
use Tk::Table;
#
%attribx = (-fill => 'x', -border => 1, -relief => 'sunken');
#
$top = new MainWindow;
$table = Table $top -relief => 'ridge';
configure $table -border => 5;
#
$entry = Entry $table;
put $table 0, 0, $entry;
Create $table 
    0, 1, 'Button', -text => "Button 1", -command => \&foo,
    0, 2, 'Entry',
    1, 2, 'Button', -text => "Button 3",
    2, 0, 'Entry',
    2, 1, 'Button', -text => "Button 5",
    2, 2, 'Entry';
#
Cell $table 'configure',[1, 2], -anchor => "se";
# $table->Row('forget', 0);
# $table->Column('forget', 1);
# $table->Column('configure', 1);
#
$table1 = Create $table 
    1, 0, 'Table', -anchor => 'nw',-border => 5, -relief => 'raised';
Create $table1
    0, 0, 'Button', -text => "But 1", -columnspan => 2, -fill => 'both',
    0, 2, 'Button', -text => "But 2", -rspan => 2, -fill => 'both',
    1, 0, 'Button', -text => "But 4", -rspan => 2, -fill => 'both',
    2, 1, 'Button', -text => "But 3", -cspan => 2, -fill => 'both',
    1, 1, 'Button', -text => "But 5", -fill => 'both';
#
$table0 = Create $table 
    1, 1, 'Table', -border => 10, -relief => 'sunken', -fill => 'both';
#
$tablet = Create $table0 0, 1, 'Table',-fill => 'x';
Create $tablet 
    0, 0, 'Label', -text => '1', %attribx,
    0, 1, 'Label', -text => '2', %attribx;
#
$tables = Create $table0 2, 1, 'Table',-fill => 'x';
Create $tables 
    0, 0, 'Label', -text => '4', -fill => 'x', -border => 1, -relief => 'sunken',
    0, 1, 'Label', -text => '3', -fill => 'x', -border => 1, -relief => 'sunken';
#
($table3, $scrollv1, $scrollv2) = Create $table0 
    1, 1, 'Table', -height => 200,
    1, 0, 'Scrollbar', -orient => 'vertical', -fill => 'y',
    1, 2, 'Scrollbar', -orient => 'vertical', -fill => 'y';
configure $scrollv1 -command => ['yview', $table3];
configure $scrollv2 -command => ['yview', $table3];
configure $table3 
    -yscrollcommand => sub { $scrollv1->set(@_); $scrollv2->set(@_); },
    -col => [$tablet, $tables];
#
Create $table3 
    0, 0, 'Text', -width => 6, -height => 8,
    5, 1, 'Button', -text => "Button 3", -command => sub {$table3->under(50, 50);},
    1, 0, 'Button', -text => "Button 4",
    1, 1, 'Entry',
    2, 0, 'Entry',
    2, 1, 'Button', -text => "Button 5",
    3, 0, 'Button', -text => "Button 6",
    3, 1, 'Entry',
    4, 0, 'Entry',
    4, 1, 'Button', -text => "Button 7",
    5, 0, 'Button', -text => "Button 8";
#
$table4 = Create $table3 0, 1, 'Table', -border => 2, -relief => 'groove';
Create $table4 
    1, 0, 'Button', -text => "Button 4",
    1, 1, 'Entry',
    2, 0, 'Entry',
    2, 1, 'Button', -text => "Button 5",
    3, 0, 'Button', -text => "Button 6",
    3, 1, 'Entry',
    4, 0, 'Entry';
#
$table->pack(-fill => 'both', -expand => 1);
# print $table->tablex(20)," ",$table->tabley(20),"\n";
MainLoop;

sub foo {
    @dim =  $table->dimension;
    $, = ' ';
    for($i=0; $i<$dim[0];$i++) {
	print "$i : ",$table->Row('size', $i)," ",
	$table->Column('size',$i),"\n";
    }
}

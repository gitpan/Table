# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl test.pl'

######################### We start with some black magic to print on failure.

# Change 1..1 below to 1..last_test_to_print .
# (It may become useful if the test is moved to ./t subdirectory.)

BEGIN { $| = 1; print "1..39\n"; }
END {print "not ok 1\n" unless $loaded;}
use Table;
$loaded = 1;
print "ok loaded\n";

$t = Table::fromCSV("aaa.csv");
if ($t) {
  print "ok 1 fromCSV\n";
} else {
  print "not ok 1 fromCSV\n";
}
if ($t->colIndex('Grams "(a.a.)"/100g sol.') == 3) {
  print "ok 2 colIndex\n";
} else {
  print "not ok 2 colIndex\n";
}
if ($t->nofCol() == 6) {
  print "ok 3 nofCol\n";
} else {
  print "not ok 3 nofCol\n";
}
if ($t->nofRow() == 9) {
  print "ok 4 nofRow\n";
} else {
  print "not ok 4 nofRow\n";
}
if ($t->html()) {
  print "ok 5 html\n";
} else {
  print "not ok 5 html\n";
}
if ($t->html2()) {
  print "ok 6 html2\n";
} else {
  print "not ok 6 html2\n";
}
if ($t->nofCol() == 6) {
  print "ok 7 nofCol\n";
} else {
  print "not ok 7 nofCol\n";
}
$fun = sub {return lc;};
if ($t->colMap(0,$fun)) {
  print "ok 8 colMap\n";
} else {
  print "not ok 8 colMap\n";
}
$t->colMap(0,sub {return ucfirst});
if (($row = $t->delRow(0)) && $t->nofRow==8) {
  print "ok 9 delRow()\n";
} else {
  print "not ok 9 delRow()\n";
}
if ($t->addRow($row) && $t->nofRow==9) {
  print "ok 10 addRow()\n";
} else {
  print "not ok 10 addRow()\n";
}
if ((@rows = $t->delRows([0,2,3])) && $t->nofRow==6) {
  print "ok 11 delRows()\n";
} else {
  print "not ok 11 delRows()\n";
}
$t->addRow(shift @rows,1);
$t->addRow(shift @rows,1);
$t->addRow(shift @rows,0);
if ($t->nofRow==9) {
  print "ok 12 delRows() & addRow()\n";
} else {
  print "not ok 12 delRows() & addRow()\n";
}
if (($col = $t->delCol("Solvent")) && $t->nofCol==5) {
  print "ok 13 delCol()\n";
} else {
  print "not ok 13 delCol()\n";
}
if ($t->addCol($col, "Solvent",2) && $t->nofCol==6) {
  print "ok 14 addCol()\n";
} else {
  print "not ok 14 addCol()\n";
}
if ((@cols = $t->delCols(["Temp, C","Amino acid","Entry"])) && $t->nofCol==3) {
  print "ok 15 delCols()\n";
} else {
  print "not ok 15 delCols()\n";
}
$t->addCol(shift @cols,"Temp, C",2);
$t->addCol(shift @cols,"Entry",0);
$t->addCol(shift @cols,"Amino acid",0);
if ($t->nofCol==6) {
  print "ok 16 delCols() & addCol()\n";
} else {
  print "not ok 16 delCols() & addCol()\n";
}
if ($t->rowRef(3)) {
  print "ok 17 rowRef()\n";
} else {
  print "not ok 17 rowRef()\n";
}
if ($t->rowRefs(undef)) {
  print "ok 18 rowRefs()\n";
} else {
  print "not ok 18 rowRefs()\n";
}
if ($t->row(3)) {
  print "ok 19 row()\n";
} else {
  print "not ok 19 row()\n";
}
if ($t->colRef(3)) {
  print "ok 20 colRef()\n";
} else {
  print "not ok 20 colRef()\n";
}
if ($t->colRefs(["Temp, C", "Amino acid", "Solvent"])) {
  print "ok 21 colRefs()\n";
} else {
  print "not ok 21 colRefs()\n";
}
if ($t->col(3)) {
  print "ok 22 col()\n";
} else {
  print "not ok 22 col()\n";
}
if ($t->rename("Entry", "New Entry")) {
  print "ok 23 rename()\n";
} else {
  print "not ok 23 rename()\n";
}
$t->rename("New Entry", "Entry");
@t = $t->col("Entry");
$t->replace("Entry", [1..$t->nofRow()], "New Entry");
if ($t->replace("New Entry",\@t, 'Entry')) {
  print "ok 24 replace()\n";
} else {
  print "not ok 24 replace()\n";
}
if ($t->swap("Amino acid","Entry")) {
  print "ok 25 swap()\n";
} else {
  print "not ok 25 swap()\n";
}
$t->swap("Amino acid","Entry");
if ($t->elm(3,"Temp, C")==79) {
  print "ok 26 elm()\n";
} else {
  print "not ok 26 elm()\n";
}
if (${$t->elmRef(3,"Temp, C")}==79) {
  print "ok 27 elmRef()\n";
} else {
  print "not ok 27 elmRef()\n";
}
$t->setElm(3,"Temp, C", 100);
if ($t->elm(3,"Temp, C")==100) {
  print "ok 28 setElm()\n";
} else {
  print "not ok 28 setElm()\n";
}
$t->setElm(3,"Temp, C",79);
if ($t->sort('Ref No.',1,1,'Temp, C',1,0)) {
  print "ok 29 sort()\n";
} else {
  print "not ok 29 sort()\n";
}
if (($t2=$t->match_pattern('$_->[0] =~ /^L/ && $_->[3]<0.2')) && $t2->nofRow()==4) {
  print "ok 30 match_pattern()\n";
} else {
  print "not ok 30 match_pattern()\n";
}
if (($t2=$t->match_string('allo|cine')) && $t2->nofRow()==4) {
  print "ok 31 match_string()\n";
} else {
  print "not ok 31 match_string()\n";
}
if ($t2=$t->clone()) {
  print "ok 32 clone()\n";
} else {
  print "not ok 32 clone()\n";
}
if ($t2=$t->subTable([2..4],[0..($t->nofCol()-1)])) {
  print "ok 33 subTable()\n";
} else {
  print "not ok 33 subTable()\n";
}
if ($t2=$t->subTable([2..4],undef)) {
  print "ok 34 subTable(\$rowIdcsRef,undef)\n";
} else {
  print "not ok 34 subTable(\$rowIdcsRef,undef)\n";
}
if (($t2=$t->subTable(undef,[0..($t->nofCol-1)]))&& ($t2->nofRow() == 9)) {
  print "ok 35 subTable(undef,\$colIDsRef)\n";
} else {
  print "not ok 35 subTable(undef,\$colIDsRef)\n";
}
if ($t->rowMerge($t2) && $t->nofRow()==18) {
  print "ok 36 rowMerge()\n";
} else {
  print "not ok 36 rowMerge()\n";
}
$t->delRows([9..$t->nofRow-1]);
$t2=$t->subTable([0..($t->nofRow-1)],[1]);
$t2->rename(0, "new column");
if ($t->colMerge($t2) && $t->nofCol()==7) {
  print "ok 37 colMerge()\n";
} else {
  print "not ok 37 colMerge()\n";
}
$t->delCol('new column');
$t->sort('Entry',1,0);

$t2=Table::fromCSV('aaa.csv');
if (equal($t->rowRefs(), $t2->rowRefs())) {
  print "ok 38 overall\n";
} else {
  print "not ok 38 overall\n";
}

# use DBI;
# $dbh= DBI->connect("DBI:mysql:test", "test", "") or die $dbh->errstr;
# $t = Table::fromSQL($dbh, "show tables");
# print $t->csv;

# @_ in match_
package FOO; @ISA = qw(Table);

1;

package main;

$foo=new FOO([[11,12],[21,22],[31,32]],['header1','header2'],0);
if ($foo->csv) {
  print "ok 39 Inheritance\n";
} else {
  print "not ok 39 Inheritance\n";
}

sub equal {
  my ($data, $data2) = @_;
  my ($i ,$j);
  return 0 if (scalar @$data != scalar @$data2);
  for ($i=0; $i< scalar @$data; $i++) {
    return 0 if (scalar @{$data->[$i]} != scalar @{$data2->[$i]});
    for ($j=0; $j< scalar @{$data->[0]}; $j++) {
      return 0 if $data->[$i]->[$j]!= $data2->[$i]->[$j];
    }
  }
  return 1;
}

package Table;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK);

require Exporter;
require AutoLoader;

@ISA = qw(Exporter AutoLoader);
# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
@EXPORT = qw(
	
);
$VERSION = '1.16';

sub new {
  my ($class, $data, $header, $type, $enforceCheck) = @_;
  $type = 0 unless defined($type); 
  $header=[] unless defined($header);
  $data=[] unless defined($data);
  $enforceCheck = 1 unless defined($enforceCheck);
  die "new Table: Size of data does not match header\n"
    if (($type && (scalar @$data) && $#{$data} != $#{$header}) ||
        (!$type && (scalar @$data) && $#{$data->[0]} != $#{$header}));
  my $colHash= {};
  for (my $i = 0; $i < scalar @$header; $i++) {
    my $elm = $header->[$i];
    die "Invalid column name: $elm" unless ($elm =~ /\D/);
    die "Undefined column name at \$header->[$i]" unless $elm;
    die "Header name ".$colHash->{$elm}." appears more than once" if defined($colHash->{$elm});
    $colHash->{$elm} = $i;
  }
  if ($enforceCheck && scalar @$data > 0) {
    my $size=scalar @{$data->[0]};
    for (my $j =1; $j<scalar @$data; $j++) {
      die "Inconsistant array size at data[$j]" unless (scalar @{$data->[$j]} == $size);
    }
  }
  my $self={ data=>$data, header=>$header, type=>$type, colHash=>$colHash};
  return bless $self, $class;
}

# translate a column name into its position in the header
# (also in column-based table)
sub colIndex {
  my ($self, $colID) = @_;
  if ($colID =~ /\D/) {
    my $i = $self->{colHash}->{$colID};
    return -1 unless defined($i);
    return $i;
  }
  return $colID; # assume an index already
}

sub nofCol {
  my $self = shift;
  return scalar @{$self->{header}};
}

sub nofRow {
  my $self = shift;
  return 0 if (scalar @{$self->{data}} == 0);
  return ($self->{type})?
    scalar @{$self->{data}->[0]} : scalar @{$self->{data}};
}

# still need to consider quotes and comma in string
# need to get csv specification
sub csvEscape {
  my $s = shift;
  $s =~ s/"/""/g;
  if ($s =~ /[",]/) { return "\"$s\""; }
  return $s;
}

# output table in CSV format
sub csv {
  my $self=shift;
  my ($status, @t);
  my $s=join(",", map {csvEscape($_)} @{$self->{header}}) . "\n";
######  $self->rotate if $self->{type};
  if ($self->{data}) {
    $self->rotate() if ($self->{type});
    my $data=$self->{data};
    for (my $i=0; $i<=$#{$data}; $i++) {
      $s .= join(",", map {csvEscape($_)} @{$data->[$i]}) . "\n";
    }
  }
  return $s;
}

# output table in HTML format
sub html {
  my ($self, $colors, $specs) = @_;
  my %sp = ();
  my $s = "";
  if (ref $specs eq 'HASH') {
	%sp = %$specs;
  } else {
	$sp{BORDER} = 1;
  } 
  $s = "<TABLE ";
  foreach my $key (keys %sp) {
	$s .= "$key=$sp{$key} ";
  }
  $s .= ">\n";
  my $header=$self->{header};
  my @BG_COLOR=("#D4D4BF","#ECECE4","#CCCC99");
  @BG_COLOR=@$colors if ((ref($colors) eq "ARRAY") && (scalar @$colors==3));
  $s .= "<TR BGCOLOR=\"" . $BG_COLOR[2] . "\"><TH>" .
    join("</TH><TH>", @$header) . "</TH></TR>\n";
  $self->rotate() if $self->{type};
  my $data=$self->{data};
  for (my $i=0; $i<=$#{$data}; $i++) {
    $s .= "<TR BGCOLOR=\"" . $BG_COLOR[$i%2] . "\">";
    for (my $j=0; $j<=$#{$header}; $j++) {
      $s .= "<TD>";
      $s .= ($data->[$i][$j])?$data->[$i][$j]:"&nbsp;";
      $s .= "</TD>";
    }
    $s .= "</TR>\n";
  }
  $s .= "</TABLE>\n";
  return $s;
}

# output table in HTML format, with table orientation rotated,
# so that each HTML table row is a column in the table
# This is useful for a slim table (few columns but many rows)
sub html2 {
  my ($self, $colors, $specs) = @_;
  my %sp = ();   
  my $s = "";  
  if (ref $specs eq 'HASH') {
        %sp = %$specs;
  } else {
        $sp{'BORDER'} = 1;
  }
  $s = "<TABLE ";
  foreach my $key (keys %sp) {
        $s .= "$key=$sp{$key} ";
  }
  $s .= ">\n";
  my $header=$self->{header};
  my @BG_COLOR=("#D4D4BF","#ECECE4","#CCCC99");
  @BG_COLOR=@$colors if ((ref($colors) eq "ARRAY") && (scalar @$colors==3));
  $self->rotate() unless $self->{type};
  my $data=$self->{data};
  for (my $i = 0; $i <= $#{$header}; $i++) {
    $s .= "<TR><TH BGCOLOR=\"" . $BG_COLOR[2] . "\">" .
          $header->[$i] . "</TD>";
    for (my $j=0; $j<=$#{$data->[0]}; $j++) {
      $s .= "<TD BGCOLOR=" . $BG_COLOR[$j%2] . ">";
      $s .= ($data->[$i][$j])?$data->[$i][$j]:'&nbsp;';
      $s .= "</TD>";
    }
    $s .= "</TR>\n";
  }
  $s .= "</TABLE>\n";
  return $s;
}

# apply a $fun to each elm in a col 
sub colMap {
  my ($self, $colID, $fun) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  $self->rotate() unless $self->{type};
  my $ref = $self->{data}->[$c];
  my @tmp = map {&$fun($_)} @$ref;
  $self->{data}->[$c] = \@tmp;
  return 1;
} 

sub addRow {
  my ($self, $rowRef, $rowIdx) = @_;
  my $numRow=$self->nofRow();
  my @t;
  die "addRow: size of added row does not match those in the table\n"
	if scalar @$rowRef != $self->nofCol();
  $rowIdx=$numRow unless defined($rowIdx);
  return undef unless defined $self->checkNewRow($rowIdx);
  $self->rotate() if $self->{type};
  my $data=$self->{data};
  if ($rowIdx == 0) {
    unshift @$data, $rowRef;
  } elsif ($rowIdx == $numRow) {
    push @$data, $rowRef;
   } else {
    @t = splice @$data, $rowIdx;
    push @$data, $rowRef, @t;
  }
  return 1;
}

sub delRow {
  my ($self, $rowIdx ) = @_;
  return undef unless defined $self->checkOldRow($rowIdx);
  $self->rotate() if $self->{type};
  my $data=$self->{data};
  my @dels=splice(@$data, $rowIdx, 1);
  return shift @dels;
}                                                                               

sub delRows {
  my ($self, $rowIdcsRef) = @_;
  my $rowIdx;
  my @indices = sort { $b <=> $a } @$rowIdcsRef;
  my @dels=();
  foreach $rowIdx (@indices) {
    push @dels, $self->delRow($rowIdx);
  }
  return @dels;
}   

# append a column to the table, input is a referenceof_array

sub addCol {
  my ($self, $colRef, $colName, $colIdx) = @_;
  my $numCol=$self->nofCol();
  my @t;
  die "addCol: size of added col does not match rows in the table\n" 
	if scalar @$colRef != $self->nofRow(); 
  $colIdx=$numCol unless defined($colIdx);
  return undef unless defined $self->checkNewCol($colIdx, $colName);
  $self->rotate() unless $self->{type};
  my $data=$self->{data};
  my $header=$self->{header};
  if ($colIdx == 0) {
    unshift @$header, $colName;
  } elsif ($colIdx == $numCol) {
    push @$header, $colName;
  } else {
    @t = splice @$header, $colIdx;
    push @$header, $colName, @t;
  }

  if ($colIdx == 0) {
    unshift @$data, $colRef;
  } elsif ($colIdx == $numCol) {
    push @$data, $colRef;
  } else {
    @t = splice @$data, $colIdx;
    push @$data, $colRef, @t;
  }

  for (my $i = 0; $i < scalar @$header; $i++) {
    my $elm = $header->[$i];
    $self->{colHash}->{$elm} = $i;
  }
  return 1;
}

sub delCol {
  my ($self, $colID) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  my $header=$self->{header};
  my $name=$self->{header}->[$c];
  splice @$header, $c, 1;
  $self->rotate() unless $self->{type};
  my $data=$self->{data};
  delete $self->{colHash}->{$name};
  for (my $i = 0; $i < scalar @$header; $i++) {
    my $elm = $header->[$i];
    $self->{colHash}->{$elm} = $i;
  }
  my @dels=splice @$data, $c, 1;
  return shift @dels;
}                                                                               

sub delCols {
  my ($self, $colIDsRef) = @_;
  my $idx;
  my @indices = map { $self->colIndex($_) } @$colIDsRef;
  @indices = sort { $b <=> $a } @indices;

  my @dels=();
  foreach my $colIdx (@indices) {
    push @dels, $self->delCol($colIdx);
  }
  return @dels;
}  


sub rowRef {
  my ($self, $rowIdx) = @_;
  return undef unless defined $self->checkOldRow($rowIdx);
  $self->rotate if $self->{type};
  return $self->{data}->[$rowIdx];
}

sub rowRefs {
  my ($self, $rowIdcsRef) = @_;
  $self->rotate if $self->{type};
  return $self->{data} unless defined $rowIdcsRef;
  my @ones = ();
  my $rowIdx;
  foreach $rowIdx (@$rowIdcsRef) {
    push @ones, $self->rowRef($rowIdx);
  }
  return \@ones;
}

sub row {
  my ($self, $rowIdx) = @_;
  my $data = $self->{data};
  return undef unless defined $self->checkOldRow($rowIdx);
  if ($self->{type}) {
    my @one=(); 
    for (my $i = 0; $i < scalar @$data; $i++) {
      push @one, $data->[$i]->[$rowIdx];
    }
    return @one;
  } else {
    return @{$data->[$rowIdx]};
  }
}

sub colRef {
  my ($self, $colID) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  $self->rotate() unless $self->{type};
  return $self->{data}->[$c];
}

sub colRefs {
  my ($self, $colIDsRef) = @_;
  $self->rotate unless $self->{type};
  return $self->{data} unless defined $colIDsRef;
  my @ones = ();
  my $colID;
  foreach $colID (@$colIDsRef) {
    push @ones, $self->colRef($colID);
  }
  return \@ones;
}

sub col {
  my ($self, $colID) = @_;
  my $data = $self->{data};
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  if (!$self->{type}) {
    my @one=();
    for (my $i = 0; $i < scalar @$data; $i++) {
      push @one, $data->[$i]->[$c];
    }
    return @one;
  } else {
    return () unless ref($data->[$c]) eq "ARRAY";
    return @{$data->[$c]};
  }
}

sub rename {
  my ($self, $colID, $name) = @_;
  my $oldName;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  $oldName=$self->{header}->[$c];
  return if ($oldName eq $name);
  return undef unless defined $self->checkNewCol($c, $name);
  $self->{header}->[$c]=$name;
  $self->{colHash}->{$oldName}=undef;
  $self->{colHash}->{$name}=$c;
  return 1;
}

sub replace{
  my ($self, $oldColID, $newColRef, $newName) = @_;
  my $oldName;
  my $c=$self->checkOldCol($oldColID);
  return undef unless defined $c;
  $oldName=$self->{header}->[$c];
  $newName=$oldName unless defined($newName);
  unless ($oldName eq $newName) {
  	return undef unless defined $self->checkNewCol($c, $newName);
  }
  die "New column size ".(scalar @$newColRef)." must be ".$self->nofRow() unless (scalar @$newColRef==$self->nofRow());
  $self->rename($c, $newName);
  $self->rotate() unless $self->{type};
  my $old=$self->{data}->[$c];
  $self->{data}->[$c]=$newColRef;
  return $old;
}

sub swap{
  my ($self, $colID1, $colID2) = @_;
  my $c1=$self->checkOldCol($colID1);
  return undef unless defined $c1;
  my $c2=$self->checkOldCol($colID2);
  return undef unless defined $c2;
  my $name1=$self->{header}->[$c1];
  my $name2=$self->{header}->[$c2];

  $self->{header}->[$c1]=$name2;
  $self->{header}->[$c2]=$name1;
  $self->{colHash}->{$name1}=$c2;
  $self->{colHash}->{$name1}=$c1;
  $self->rotate() unless $self->{type};
  my $data1=$self->{data}->[$c1];
  my $data2=$self->{data}->[$c2];
  $self->{data}->[$c1]=$data2;
  $self->{data}->[$c2]=$data1;
  return 1;
}

sub checkOldRow {
  my ($self, $rowIdx) = @_;
  my $maxIdx=$self->nofRow()-1;
  unless (defined $rowIdx) {
	print STDERR " Invalid row index\n";
	return undef;
  }
  if ($rowIdx<0 || $rowIdx>$maxIdx) {
	print STDERR  "Row index out of range [0..$maxIdx]" ;
	return undef;
  }
  return $rowIdx;
}

sub checkNewRow {
  my ($self, $rowIdx) = @_;
  my $maxIdx=$self->nofRow()-1;
  unless (defined $rowIdx) {
	print STDERR "Invalid row index: $rowIdx \n";
	return undef;
  } 
  $maxIdx+=1;
  if ($rowIdx<0 || $rowIdx>$maxIdx) {
  	print STDERR  "Row index out of range [0..$maxIdx]" ;
	return undef;
  }
  return $rowIdx;
}

sub checkOldCol {
  my ($self, $colID) = @_;
  my $c=$self->colIndex($colID);
  if ($c < 0) {
  	print STDERR "Invalid column $colID";
	return undef;
  } 
  return $c;
}

sub checkNewCol {
  my ($self, $colIdx, $colName) = @_;
  my $numCol=$self->nofCol();
  unless (defined $colIdx) { 
      	print STDERR "Invalid column index $colIdx";
      	return undef;
  }	
  if ($colIdx<0 || $colIdx>$numCol) {
      	print STDERR "Column index $colIdx out of range [0..$numCol]";
  	return undef;
  }	
  if (defined $self->{colHash}->{$colName} ) {
	print STDERR "Column name $colName already exists" ;
	return undef;
  }
  unless ($colName =~ /\D/) { 
    	print STDERR "Invalid column name $colName" ;
	return undef;
  }
  return $colIdx;
}

sub elm {
  my ($self, $rowIdx, $colID) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  return undef unless defined $self->checkOldRow($rowIdx);
  return ($self->{type})?
    $self->{data}->[$c]->[$rowIdx]:
    $self->{data}->[$rowIdx]->[$c];
}

sub elmRef {
  my ($self, $rowIdx, $colID) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  return undef unless defined $self->checkOldRow($rowIdx);
  return ($self->{type})?
    \$self->{data}->[$c]->[$rowIdx]:
    \$self->{data}->[$rowIdx]->[$c];
}

sub setElm {
  my ($self, $rowIdx, $colID, $val) = @_;
  my $c=$self->checkOldCol($colID);
  return undef unless defined $c;
  return undef unless defined $self->checkOldRow($rowIdx);
  if ($self->{type}) {
    $self->{data}->[$c]->[$rowIdx]=$val;
  } else {
    $self->{data}->[$rowIdx]->[$c]=$val;
  }
  return 1;
}

# convert the internal structure of a table between row-based and column-based
sub rotate {
  my $self=shift;
  my $newdata=[];
  my $data=$self->{data};
  $self->{type} = ($self->{type})?0:1;
  for (my $i=$#{$data->[0]}; $i>=0; $i--) {
    for (my $j=$#{$data}; $j>=0;  $j--) {
      $newdata->[$i][$j]=$data->[$j][$i];
    }
  }
  $self->{data}=$newdata;
  return 1;
}

sub header {
  my $self = shift;
  return @{$self->{header}};
}

sub type {
  my $self=shift;
  return $self->{type};
}

sub data {
  my $self=shift;
  return $self->{data};
}

#  $t->sort(colID1, type1, order1, colID2, type2, order2, ... );
#  where
#    colID is a column index (integer) or name (string),
#    type is 0 for numerical and 1 for others
#    order is 0 for ascending and 1 for descending
#    Sorting is done with priority of colname1, colname2, ...

sub sort {
  my $self = shift;
  my ($str, $i) = ("", 0);
  my @cols = ();
  while (scalar @_) {
    my $c = shift;
    my $col = $self->checkOldCol($c);
    return undef unless defined $col;
    push @cols, $col;
    my $op = '<=>';
    $op = 'cmp' if shift;  				# string 
    $str .=(shift)?  "(\$b->[$i] $op \$a->[$i]) || " :
      "(\$a->[$i] $op \$b->[$i]) || " ;
    $i++;
  }
  substr($str, -3) = "";  	# removes ||  from the end of $str
  $self->rotate() if $self->{type};
  # construct a pre-ordered array
  my $fun = sub { my ($cols, $data) = @_;
  		  my @ext;
  		  @ext = map {$data->[$_]} @$cols;
  		  push @ext, $data;
  		  return \@ext;
		};
  my @preordered = map {&$fun(\@cols, $_)} @{$self->{data}};
  $self->{data} = [ map {$_->[$i]} eval "sort {$str} \@preordered;" ];
  return 1;
} 
  
# return rows as sub table in which
# a pattern $pattern is matched 
sub match_pattern {
  my ($self, $pattern) = @_;
  my @data=();
  $self->rotate() if $self->{type};
  @Table::OK= eval "map {$pattern} \@{\$self->{data}};";
  for (my $i=0; $i<$self->nofRow(); $i++) {
    push @data, $self->{data}->[$i] if $Table::OK[$i];
  }
  return new Table(\@data, \@{$self->{header}}, 0);
}

# return rows as sub table in which 
# a string elm in an array @$s is matched 
sub match_string {
  my ($self, $s) = @_;
  die unless defined($s);
  my @data=();
  $self->rotate() if $self->{type};
  @Table::OK=();
  foreach my $row_ref (@{$self->data}) {
    push @Table::OK, undef;
    foreach my $elm (@$row_ref) {
	if ($elm =~ /$s/o) {
		push @data, $row_ref;
		$Table::OK[$#Table::OK]=1;
		last;
   	}
    }
  }
  return new Table(\@data, \@{$self->{header}}, 0);
}
	
sub rowMerge {
  my ($self, $tbl) = @_;
  die "Tables must have the same number of columns" unless ($self->nofCol()==$tbl->nofCol());
  $self->rotate() if $self->{type};
  $tbl->rotate() if $tbl->{type};
  my $data=$self->{data};
#  for ($i=0; $i<$tbl->nofRow(); $i++) {
  push @$data, @{$tbl->{data}};
#  }
  return 1;
}

sub colMerge {
  my ($self, $tbl) = @_;
  die "Tables must have the same number of rows" unless ($self->nofRow()==$tbl->nofRow());
  my $col;
  foreach $col ($tbl->header) {
    die "Duplicate column $col in two tables" if defined($self->{colHash}->{$col});
  }
  my $i = $self->nofCol();
  foreach $col ($tbl->header) {
    push @{$self->{header}}, $tbl->header;
    $self->{colHash}->{$col} = $i++; 
  }
  $self->rotate() unless $self->{type};
  $tbl->rotate() unless $tbl->{type};
  my $data=$self->{data};
  for ($i=0; $i<$tbl->nofCol(); $i++) {
    push @$data, $tbl->{data}->[$i];
  }
  return 1;
}

sub subTable {
  my ($self, $rowIdcsRef, $colIDsRef) = @_;
  my @newdata=();
  my @newheader=();
  $rowIdcsRef = [0..($self->nofRow()-1)] unless defined $rowIdcsRef;
  $colIDsRef = [0..($self->nofCol()-1)] unless defined $colIDsRef; 
  for (my $i = 0; $i < scalar @{$colIDsRef}; $i++) {
    $colIDsRef->[$i]=$self->checkOldCol($colIDsRef->[$i]);
    return undef unless defined $colIDsRef;
    push @newheader, $self->{header}->[$colIDsRef->[$i]];
  }
  if ($self->{type}) {
    for (my $i = 0; $i < scalar @{$colIDsRef}; $i++) {
      my @one=();
      for (my $j = 0; $j < scalar @{$rowIdcsRef}; $j++) {
	return undef unless defined $self->checkOldRow($rowIdcsRef->[$j]);
        push @one, $self->{data}->[$colIDsRef->[$i]]->[$rowIdcsRef->[$j]];
      }
      push @newdata, \@one;
    }
  } else {
    for (my $i = 0; $i < scalar @{$rowIdcsRef}; $i++) {
      return undef unless defined $self->checkOldRow($rowIdcsRef->[$i]);	
      my @one=();
      for (my $j = 0; $j < scalar @{$colIDsRef}; $j++) {
        push @one, $self->{data}->[$rowIdcsRef->[$i]]->[$colIDsRef->[$j]];
      }
      push @newdata, \@one;
    }
  }
  return new Table(\@newdata, \@newheader, $self->{type});
}

sub clone {
  my $self = shift;
  my $data = $self->{data};
  my @newheader = @{$self->{header}};
  my @newdata = ();
  for (my $i = 0; $i < scalar @{$data}; $i++) {
    my @one=();
    for (my $j = 0; $j < scalar @{$data->[$i]}; $j++) {
      push @one, $data->[$i]->[$j];
    }
    push @newdata, \@one;
  }
  return new Table(\@newdata, \@newheader, $self->{type});
}

sub fromCSV {
  my ($name, $header) = @_;
  $header = 1 unless defined($header);
  open(SRC, $name) or die "Cannot open $name to read";
  my @data = ();
  $_=<SRC>;
  die "Empty data file" unless $_;
  my $one = parseCSV($_);
# print join("|", @$one), scalar @$one, "\n";
  my $size = scalar @$one;
  my @header;
  if ($header) {
    @header=@$one;
  } else {
    @header = map {"col$_"} (1..$size);	# name each column as col1, col2, .. etc
    push @data, $one;
  }
  while(<SRC>) {
    my $one = parseCSV($_);
#   print join("|", @$one), scalar @$one, "\n";
    die "Inconsistant column number at data entry: ".($#data+1) unless ($size==scalar @$one);
    push @data, $one;
  }
  return new Table(\@data, \@header, 0);
}

# Idea: use \ as the escape char to encode a CSV string,
# replace \ by \\ and comma inside a field by \c.
# A comma inside a field must have even number of " in front of it,
# therefore it can be distinguished from comma used as the deliminator.
# After escape, and split by comma, we unescape each field string.
#
# This parser will never be crashed by any illegal CSV format,
# it always return an array!
sub parseCSV {
  my $s=shift;
  $s =~ s/\n$//; # chop
  $s =~ s/\\/\\\\/; # escape \ => \\
  my $n = length($s);
  my ($q, $i)=(0, 0);
  while ($i < $n) {
    my $ch=substr($s, $i, 1);
    $i++;
    if ($ch eq ',' && ($q%2)) {
      substr($s, $i-1, 1)='\\c'; # escape , => \c if it's not a deliminator
      $i++;
      $n++;
    } elsif ($ch eq '"') {
      $q++;
    }
  }
  $s =~ s/^"|"$//g; # get rid of boundary ", then restore "" => "
  $s =~ s/",/,/g;
  $s =~ s/,"/,/g;
  $s =~ s/""/"/g;
  my @parts=split(/,/, $s);
  @parts = map {$_ =~ s/\\c/,/g; $_ =~ s/\\\\/\\/g; $_ } @parts;
#  my @parts2=();
#  foreach $s2 (@parts) {
#    $s2 =~ s/\\c/,/g;   # restore \c => ,
#    $s2 =~ s/\\\\/\\/g; # restore \\ => \
#    push @parts2, $s2;
#  }
  return \@parts;
}

sub fromSQL {
  my ($dbh, $sql, $vars) = @_;
  my ($sth, $header, $t);
  $sth = $dbh->prepare($sql) or die "Preparing: , ".$sth->errstr;
  my @vars=() unless defined $vars;
  $sth->execute(@vars) or die "Executing: , ".$sth->errstr;
#  $Table::ID = undef;
#  $Table::ID = $sth->{'mysql_insertid'};
  if ($sth->{NUM_OF_FIELDS}) {
    $header=$sth->{'NAME'};
    $t = new Table($sth->fetchall_arrayref(), $header, 0);
  } else {
    $t = undef;
  }
  $sth->finish;
  return $t;
}

## interface to GD::Graph
# use GD::Graph::points;
# $graph = GD::Graph::points->new(400, 300);
# $graph->plot([$t->colRef(1), $t->colRef(2)]);
 
1;

__END__


=head1 NAME

Table - Data type related to database tables, spreadsheets, CSV files, HTML table displays, etc.

=head1 SYNOPSIS

  # some cool ways to use Table.pm
  use Table;
  
  $header = ["name", "age"];
  $data = [
    ["John", 20],
    ["Kate", 18],
    ["Mike", 23]
  ]
  $t = new Table($data, $header, 0);	# Construct a table object with
					# $data, $header, $type=0 (consider 
					# $data as the rows of the table).
  print $t->csv;                        # Print out the table as a csv file.

  
  $t = Table::fromCSV("aaa.csv");       # Read a csv file into a table oject
  print $t->html;                       # Diplay a 'portrait' HTML TABLE on web. 

  use DBI;
  $dbh= DBI->connect("DBI:mysql:test", "test", "") or die $dbh->errstr;
  my $minAge = 10;
  $t = Table::fromSQL($dbh, "select * from mytable where age >= ?", [$minAge]);
					# Construct a table form an SQL 
					# database query.

  $t->sort("age", 0, 0);                # Sort by col 'age',numerical,descending
  print $t->html2;                      # Print out a 'landscape' HTML Table.  

  $row = $t->delRow(2);			# Delete the third row (index=2).
  $t->addRow($row, 4);			# Add the deleted row back as fifth row. 
  @rows = $t->delRows([0..2]);		# Delete three rows (row 0 to 2).
  $col = $t->delCol("age");		# Delete column 'age'.
  $t->addCol($col, "age",2);		# Add column 'age' as the third column
  @cols = $t->delCols(["name","phone","ssn"]); 
					# Delete 3 columns at the same time. 

  $name =  $t->elm(2,"name");	        # Element access
  $t2=$t->subTable([1, 3..4],['age', 'name']);	
					# Extract a sub-table 

  $t->rename("Entry", "New Entry");	# Rename column 'Entry' by 'New Entry'
  $t->replace("Entry", [1..$t->nofRow()], "New Entry");	
					# Replace column 'Entry' by an array of
					# numbers and rename it as 'New Entry'
  $t->swap("age","ssn");		# Swap the positions of column 'age' 
					# with column 'ssn' in the table.

  $t->colMap('name', sub {return uc});  # Map a function to a column 
  $t->sort('age',0,0,'name',1,0);	# Sort table first by the numerical 
					# column 'age' and then by the 
					# string column 'name' in descending
					# order
  $t2=$t->match_pattern('$_->[0] =~ /^L/ && $_->[3]<0.2'); 
					# Select the rows that matched the 
					# pattern specified 
  $t2=$t->match_string('John');		# Select the rows that matches 'John'   
					# in any column

  $t2=$t->clone();			# Make a copy of the table.
  $t->rowMerge($t2);			# Merge two tables
  $t->colMerge($t2);

=head1 ABSTRACT

This perl package uses perl5 objects to make it easy for
manipulating spreadsheet data among disk files, database, and Web
publishing.

A table object contains a header and a two-dimensional array of scalars.
Two class methods Table::fromCSV and Table::fromSQL allow users
to create a table object from a CSV file or a database SQL selection in a snap.

Table methods provide basic access, add, delete row(s) or column(s) operations, as well as more advanced sub-table extraction, table sorting,
record matching via keywords or patterns, table merging, and web publishing.   
Table class also provides a straightforward interface to other
popular Perl modules such as DBI and GD::Graph.

The current version of Table.pm is available at http://www.geocities.com/easydatabase

We use Table instead of Table, because Table.pm has already been used inside PerlQt module in CPAN.

=head1 INTRODUCTION

=over 4

A table object has three data members:

=item 1. $data:

a reference to an array of array-references.
It's basically a reference to a two-dimensional array.

=item 2. $header:

a reference to a string array. The array contains all the column names.

=item 3. $type = 1 or 0.

1 means that @$data is an array of table columns (fields) (column-based);
0 means that @$data is an array of table rows (records) (row-based);

=back

Row-based/Column-based are two internal implementations for a table object.
E.g., if a spreadsheet consists of two columns lastname and age.
In a row-based table, $data = [ ['Smith', 29], ['Dole', 32] ].
In a column-based table, $data = [ ['Smith', 'Dole'], [29, 32] ].

Two implementions have their pros and cons for different operations.
Row-based implementation is better for sorting and pattern matching,
while column-based one is better for adding/deleting/swapping columns.

Users only need to specify the implementation type of the table upon its
creation via Table::new, and can forget about it afterwards.
Implementation type of a table should be considered volital, because
methods switch table objects from one type into another internally.
Be advised that row/column/element references gained via table::rowRef,
table::rowRefs, table::colRef, table::colRefs, or table::elmRef may
become stale after other method calls afterwards.

For those who want to inherit from the Table class, internal method
table::rotate is used to switch from one implementation type into another.
There is an additional internal assistant data structure called
colHash in our current implementation. This hash
table stores all column names and their corresponding column index number as
key-value pairs for fast conversion. This gives users an option to use
column name wherever a column ID is expected, so that user don't have to use
table::colIndex all the time. E.g., you may say
$t->rename('oldColName', 'newColName')
instead of $t->rename($t->colIndex('oldColName'), 'newColIdx').

=head1 DESCRIPTION

=head2 Field Summary

=over 4

=item data refto_arrayof_refto_array

contains a two-dimensional spreadsheet data.

=item header refto_array

contains all column names.

=item type 0/1

0 is row-based, 1 is column-based, describe the orientation of @$data.

=back

=head2 Package Variables

=over 4

=item $Table::VERSION

=item @Table::OK

see table::match_string and table::match_pattern

# =item $Table::ID
# 
#see Table::fromSQL

=back

=head2 Class Methods

Syntax: return_type method_name ( [ parameter [ = default_value ]] [, parameter [ = default_value ]] )

If method_name starts with table::, this is an instance method, it can be used as $t->method( parameters ), where $t is a table reference.

If method_name starts with Table::, this is a class method, it should be called as
  Table::method, e.g., $t = Table::fromCSV("filename.csv").

Convensions for local variables:

  colID: either a numerical column index or a column name;
  rowIdx: numerical row index;
  rowIDsRef: reference to an array of column IDs;
  rowIdcsRef: reference to an array of row indices;
  rowRef, colRef: reference to an array of scalars;
  data: ref_to_array_of_ref_to_array of data values;
  header: ref to array of column headers;
  table: a table object, a blessed reference.

=head2 Table Creation

=over 4

=item table Table::new ( $data = [], $header = [], $type = 0, $enforceCheck = 1)

create a new table.
It returns a table object upon success, undef otherwise.
$data: points to the spreadsheet data.
$header: points to an array of column names. A column name must have at least one non-digit character.
$type: 0 or 1 for row-based/column-based spreadsheet.
$enforceCheck: 1/0 to turn on/off initial checking on the size of each row/column to make sure the data arguement indeed points to a valid structure.

=item table table::subTable ($rowIdcsRef, $colIDsRef)

create a new table, which is a subset of the original.
It returns a table object.
$rowIdcsRef: points to an array of row indices.
$colIDsRef: points to an array of column IDs.
The function make a copy of selected elements from the original table. 
Undefined $rowIdcsRef or $colIDsRef is interrpreted as all rows or all columns.

=item table table::clone

make a clone of the original.
It return a table object, equivalent to table::subTable(undef,undef).

=item table Table::fromCSV ($name, $header = 1)

create a table from a CSV file.
return a table object.
$name: the CSV file name.
$header: 0 or 1 to ignore/interrpret the first line in the file as column names,
If it is set to 0, the default column names are "col1", "col2", ...

=item table Table::fromSQL ($dbh, $sql, $vars)

create a table from the result of an SQL selection query.
It returns a table object upon success or undef otherwise.
$dbh: a valid database handler. 
Typically $dbh is obtained from DBI->connect, see "Interface to Database" or DBI.pm.
$sql: an SQL query string.
$vars: optional reference to an array of variable values, 
required if $sql contains '?'s which need to be replaced 
by the corresponding variable values upon execution, see DBI.pm for details.
Hint: in MySQL, Table::fromSQL($dbh, 'show tables from test') will also create a valid table object.

=back

=head2 Table Access and Properties

=over 4

=item int table::colIndex ($colID)

translate a column name into its numerical position, the first column has index 0 as in as any perl array.
return -1 for invalid column names.

=item int table::nofCol

return number of columns.

=item int table::nofRow

return number of rows.

=item scalar table::elm ($rowIdx, $colID)

return the value of a table element at [$rowIdx, $colID],
undef if $rowIdx or $colID is invalid. 

=item refto_scalar table::elmRef ($rowIdx, $colID)

return the reference to a table element at [$rowIdx, $colID], to allow possible modification.
It returns undef for invalid $rowIdx or $colID. 

=item refto_array table::header

return an array of column names.

=item int table::type

return the implementation type of the table (row-based/column-based) at the time,
be aware that the type of a table should be considered as volital during method calls.

=back

=head2 Table Formatting

=over 4

=item string table::csv

return a string corresponding to the CSV representation of the table.

=item string table::html ($colors = ["#D4D4BF","#ECECE4","#CCCC99"], 
			  $specs = {'name' => '', 'border => '1', ...})

return a string corresponding to a 'Portrait'-style html-tagged table.
$colors: a reference to an array of three color strings, used for backgrounds for table header, odd-row records, and even-row records, respectively. 
A defaut color array ("#D4D4BF","#ECECE4","#CCCC99")
will be used if $colors isn't defined. 
$specs: a reference to a hash that specifies other attributes such as name, border,
id, class, etc. for the TABLE tag.
The table is shown in the "Portrait" style, like in Excel.

=item string table::html2 ($colors = ["#D4D4BF","#ECECE4","#CCCC99"],
		 	   $specs = {'name' => '', 'border' => '1', ...})

return a string corresponding to a "Landscape" html-tagged table.
This is useful to present a table with many columns, but very few entries.
Check the above table::html for parameter descriptions.

=back

=head2 Table Operations

=over 4

=item int table::setElm ($rowIdx, $colID, $val)

modify the value of a table element at [$rowIdx, $colID] to a new value $val.
It returns 1 upon success, undef otherwise. 


=item int table::addRow ( $rowRef, $rowIdx = table::nofRow)

add a new row ($rowRef points to the actual list of scalars), the new row will be referred as $rowIdx as the result. E.g., addRow($aRow, 0) will put the new row as the very first row.
By default, it appends a row to the end.
It returns 1 upon success, undef otherwise.

=item refto_array table::delRow ( $rowIdx )

delete a row at $rowIdx. It will the reference to the deleted row.

=item refto_array table::delRows ( $rowIdcsRef )

delete rows in @$rowIdcsRef. It will return an array of deleted rows
upon success.

=item int table::addCol ($colRef, $colName, $colIdx = numCol)

add a new column ($colRef points to the actual data), the new column will be referred as $colName or $colIdx as the result. E.g., addCol($aCol, 'newCol', 0) will put the new column as the very first column.
By default, append a row to the end.
It will return 1 upon success or undef otherwise.

=item refto_array table::delCol ($colID)

delete a column at $colID
return the reference to the deleted column.

=item arrayof_refto_array table::delCols ($colIDsRef)

delete a list of columns, pointed by $colIDsRef. It will
return an array of deleted columns upon success.

=item refto_array table::rowRef ($rowIdx)

return a reference to the row at $rowIdx
upon success or undef otherwise.

=item refto_arrayof_refto_array table::rowRefs ($rowIdcsRef)

return a reference to array of row references upon success, undef otherwise.

=item array table::row ($rowIdx)

return a copy of the row at $rowIdx 
upon success or undef otherwise.

=item refto_array table::colRef ($colID)

return a reference to the column at $colID
upon success.

=item refto_arrayof_refto_array table::colRefs ($colIDsRef)

return a reference to array of column references upon success.

=item array table::col ($colID)

return a copy to the column at $colID
upon success or undef otherwise.

=item int table::rename ($colID, $newName)

rename the column at $colID to a $newName 
(the newName must be valid, 
and should not be idential to any other existing column names).
It returns 1 upon success
or undef otherwise.

=item refto_array table::replace ($oldColID, $newColRef, $newName)

replace the column at $oldColID by the array pointed by $newColRef, and renamed it to $newName. $newName is optional if you don't want to rename the column.
It returns 1 upon success or undef otherwise.

=item int table::swap ($colID1, $colID2)

swap two columns referred by $colID1 and $colID2.
It returns 1 upon success or undef otherwise.

=item int table::colMap ($colID, $fun)

foreach element in column $colID, map a function $fun to it.
It returns 1 upon success or undef otherwise.
This is a handy way to format a column. E.g. if a column named URL contains URL strings, colMap("URL", sub {"<a href='$_'>$_</a>"}) before html() will change each URL into a clickable hyper link while displayed in a web browser.

=item int table::sort($colID1, $type1, $order1, $colID2, $type2, $order2, ... )

sort a table in place.
First sort by column $colID1 in $order1 as $type1, then sort by $colID2 in $order2 as $type2, ...
$type is 0 for numerical and 1 for others;
$order is 0 for ascending and 1 for descending;
Sorting is done in the priority of colID1, colID2, ...
It returns 1 upon success or undef otherwise. 
Notice the table is rearranged as a result! This is different from perl's list sort, which returns a sorted copy while leave the original list untouched, 
the authors feel inplace sorting is more natural.

=item table table::match_pattern ($pattern)

return a new table consisting those rows evaluated to be true by $pattern 
upon success or undef otherwise.
Side effect: @Table::OK stores a true/false array for the original table rows. Using it, users can find out what are the rows being selected/unselected.
In the $pattern string, a column element should be referred as $_->[$colIndex]. E.g., match_pattern('$_->[0]>3 && $_->[1]=~/^L') retrieve all the rows where its first column is greater than 3 and second column starts with letter 'L'. Notice it only takes colIndex, column names are not acceptable here!

=item table table::match_string ($s)

return a new table consisting those rows contains string $s in any of its fields upon success, undef otherwise.
Side effect: @Table::OK stores a true/false array for the original table rows. 
Using it, users can find out what are the rows being selected/unselected.
The $s string is actually treated as a regular expression and 
applied to each row element, therefore one can actually specify several keywords 
by saying, for instance, match_string('One|Other').

=back

=head2 Table-Table Manipulations

=over 4

=item int table::rowMerge ($tbl)

Append all the rows in the table object $tbl to the original rows.
The merging table $tbl must have the same number of columns as the original.
It returns 1 upon success, undef otherwise.
The table object $tbl should not be used afterwards, since it becomes part of
the new table.

=item int table::colMerge ($tbl)

Append all the columns in table object $tbl to the original columns. 
Table $tbl must have the same number of rows as the original.
It returns 1 upon success, undef otherwise.
Table $tbl should not be used afterwards, since it becomes part of
the new table.

=back

=head2 Internal Methods

All internal methods are mainly implemented for used by 
other methods in the Table class. Users should avoid using them.
Nevertheless, they are listed here for developers who 
would like to understand the code and may derive a new class from Table.

=over 4

=item int table::rotate

convert the internal structure of a table between row-based and column-based.
return 1 upon success, undef otherwise.

=item string csvEscape($rowRef)

Encode an array of scalars into a CSV-formatted string.

=item refto_array parseCSV($string)

Break a CSV encoded string to an array of scalars (check it out, we did it the cool way).

=back

=head1 INTERFACE TO OTHER SOFTWARES

  Spreadsheet is a very generic type, therefore Table class provides an easy
  interface between databases, web pages, CSV files, graphics packages, etc.

  Here is a summary (partially repeat) of some classic usages of Table.

=head2 Interface to Database and Web

  use DBI;

  $dbh= DBI->connect("DBI:mysql:test", "test", "") or die $dbh->errstr;
  my $minAge = 10;
  $t = Table::fromSQL($dbh, "select * from mytable where age >= ?", [$minAge]);
  print $t->html;

=head2 Interface to CSV

  $t = fromCSV("mydata.csv");
  $t->sort(1,1,0);
  print $t->csv;

=head2 Interface to Graphics Package

  use GD::Graph::points;

  $graph = GD::Graph::points->new(400, 300);
  $t2 = $t->match('$_->[1] > 20 && $_->[3] < 35.7');
  my $gd = $graph->plot($t->colRefs([0,2]));
  open(IMG, '>mygraph.png') or die $!;
  binmode IMG;
  print IMG $gd->png;
  close IMG;

=head1 AUTHOR

Copyright 1998-2000, Yingyao Zhou & Guangzhou Zou. All rights reserved.

It was first written by Zhou in 1998, significantly improved and maintained by Zou since 1999. The authors thank Tong Peng and Yongchuang Tao for valuable suggestions.

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

Please send bug reports and comments to: easydatabase@yahoo.com. When sending
bug reports, please provide the version of Table.pm, the version of
Perl.

=head1 SEE ALSO

  DBI, GD::Graph.

=cut


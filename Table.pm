package Tk::BLT::Table; 
require DynaLoader;
require Tk::Widget;
use AutoLoader;
use Carp;
 
@ISA = qw(DynaLoader Tk::Widget);
 
Tk::Widget->Construct('Table');
 
bootstrap Tk::BLT::Table;
 
sub Tk_cmd { \&Tk::table }

@CellOptions = ( 
		"-anchor", "-columnspan", "-fill", "-padx", "-pady",
		"-ipadx", "-ipady", "-reqheight", "-reqwidth",
		"-rspan", "-cspan", "-rowspan"
		);

sub Create;
sub put;

sub createargs {
    my ($args, $table) = @_;
    my (@result) = ();
    my ($t_opt, $f_opt);
    my ($val,$opt) = ('', '');
    while(($val, $opt) = each(%$args)) {
	if (grep(/^$val/, @$table)) {
	    $t_opt = {} if !defined $t_opt;
	    $t_opt->{$val} = $opt;
	} else {
	    $f_opt = {} if !defined $f_opt;
	    $f_opt->{$val} = $opt;
	}
    }
    return ($t_opt, $f_opt);
}

sub dispatch {
    my ($args) = @_;
    my ($row, $col, $entry, $res, $val);
    $row = shift @$args;
    $col = shift @$args;
    $entry = shift @$args;
    while( defined ($val = shift @$args)) {
	if ($val =~ /^\d+$/) {
	    unshift @$args, $val;
	    last; 
	}
	$res = {} if !defined $res;
	$res->{$val} = shift @$args;
    }
    return ($row, $col, $entry, $res);
}

sub CREATE {
    my ($table, @args) = @_;
    my (@result) = ();

    while (1) {
	my ($row, $col, $entry, $args) = dispatch(\@args);
	my ($t_arg, $f_arg, $obj) = (undef, undef, undef);
	last if !defined $row;
	croak "Invalid row" if $row < 0;
	croak "Invalid column" if !defined $col || $col < 0;
	croak "Invalid widget" if !defined $entry;
	if (ref $entry) {
	    croak "Table is not the parent of widget" 
		if $entry->parent ne $table;
	    $t_arg = $args if defined $args;
	} else {
	    ($t_arg, $f_arg) = createargs($args, \@CellOptions)
		if defined $args;
	    if (defined $f_arg) {
		$entry = eval { $table->$entry(%$f_arg); };
	    } else {
		$entry = eval { $table->$entry(); };
	    }	
	    croak $@ if $@;
	}
	if (defined $t_arg) {
	    $obj = eval { $table->create($entry->PathName, "$row,$col", %$t_arg) };
	} else {
	    $obj = eval { $table->create($entry->PathName, "$row,$col") };
	}
	croak "$@" if ($@);
	$table->{'_matrice_'}[$row][$col] = $entry;
	push(@result, $entry);
    }
    return @result;
}

*Create = \&CREATE;
*put = \&CREATE;

sub Forget {
    my ($table, $type, $indice) = @_;
    my (@args) = ();
    if ($type eq 'row') {
	my $entry = 0;
	foreach $entry (@{$table->{'_matrice_'}[$indice]}) {
	    push(@args, $entry->PathName) if defined $entry;
	}
    } elsif ($type eq 'column') {
	my $row = 0;
	foreach $row (@{$table->{'_matrice_'}}) {
	    push(@args, $row->[$indice]->PathName) if defined $row->[$indice];
	}
    } else {
	if (ref($indice) eq "ARRAY") {
	    @args = ($table->{'_matrice_'}[$$indice[0]][$$indice[1]]->PathName)
		if defined $table->{'_matrice_'}[$$indice[0]]
		    && defined $table->{'_matrice_'}[$$indice[0]][$$indice[1]];
	} else {
	    @args = ($indice->PathName);
	}
    }
    croak "Invalid indice $indice" if $#args < 0;
    my @obj = eval { $table->forget(@args) }; 
    if ($@) {
	croak "$@";
    } else {
	if ($type eq 'row') {
	    undef @{$table->{'_matrice_'}[$indice]};
	} elsif ($type eq 'column') {
	    my @vect = @{$table->{'_matrice_'}};
	    my $i;
	    for ($i = 0; $i <= $#vect; $i++) {
		undef $table->{'_matrice_'}[$i][$indice]
		    if defined $table->{'_matrice_'}[$i][$indice];
	    }
	} else {
	    if (ref($indice) eq "ARRAY") {
		undef $table->{'_matrice_'}[$$indice[0]][$$indice[1]];
	    } else {
		my @vect = @{$table->{'_matrice_'}};
		my ($i, $j, @rev) = (0, 0, 0);
		for ($i = 0; $i <= $#vect; $i++) {
		    @rev = @{$table->{'_matrice_'}[$i]};
		    for($j = 0; $j <= $#rev; $j++) {
			if (defined $table->{'_matrice_'}[$i][$j] &&
			    $table->{'_matrice_'}[$i][$j] eq $indice) {
			    undef $table->{'_matrice_'}[$i][$j];
			}
		    }
		}
	    }
	}
    }
    return @obj;
}
    
sub dimension {
    my $table = shift;
    return ($table->Row('dimension'), $table->Column('dimension'));
}

sub location {
    my($table, $pos) = @_;
    return ($table->Row('location', $pos), $table->Column('location', $pos));
}

sub under {
    my $table = shift;
    my @obj = eval { $table->Under(@_); };
    return $table->{'_matrice_'}[$obj[0]][$obj[1]] if !$@;
}

sub get {
    my $table = shift;
    my @obj = eval { $table->Get(@_); };
    return $table->{'_matrice_'}[$obj[0]][$obj[1]] if !$@;
}

sub Row {
    my ($table,$opt, $row, @args) = @_;
    return $table->Forget('row', $row, @args) if $opt eq 'forget';
    my @obj = eval { $table->row($opt, [$row], @args); }; 
    croak "$@" if ($@);
    return @obj;
}

sub Column {
    my ($table,$opt, $column, @args) = @_;
    return $table->Forget('column', $column, @args) if $opt eq 'forget';
    my @obj = eval { $table->column($opt, [$column], @args); }; 
    croak "$@" if ($@);
    return @obj;
}

sub Cell {
    my ($table, $opt, $cell, %args) = @_;
    return $table->Forget('cell', $cell) if $opt eq 'forget';
    if (ref($cell) eq "ARRAY") {
	$cell = $table->get($$cell[0], $$cell[1]);
    }
    my @obj = eval { $table->cell($opt, $cell->PathName, %args) };
    croak "$@" if ($@);
    return @obj;

}

1;

__END__
 

#!/usr/bin/perl -w

use strict;

if (@ARGV != 1) {
	print("usage: cut_move.pl <file>\n");
	exit;
}


my ($file)  = @ARGV;

open(FH, '<', $file) or die "opening $file failed";

my $out_start;
my $avg_len = 500;
my @avg_arr;
my $avg = 0;
my $avg_ptr = 0;
for (0 .. $avg_len-1) {
	$avg_arr[$_] = 0;
}
my $lines = 0;
while (<FH>) {
	next unless (/^\s*([\d.]+)\s/);

	if (/^\s*([\d.]+)\s+CMPL as (\S+) (\S+) (\S+) x.y (\S+) (\S+) z (\S+) (\S+) (\S+) e (\S+)$/) {
		my ($ts, $as_x1, $as_x2, $as_y, $x, $y, $z1, $z2, $z3, $e) =
			($1, $2, $3, $4, $5, $6, $7, $8, $9, $10);
		$avg -= $avg_arr[$avg_ptr];
		$avg += $as_y;
		$avg_arr[$avg_ptr] = $as_y;
		if (++$avg_ptr == $avg_len) {
				$avg_ptr = 0;
		}
		if (!defined $out_start) {
			$out_start = $ts;
		}
		my $t = sprintf("%8.6f", $ts - $out_start);
		my $a = $avg / $avg_len;
		my $_y = $avg_arr[($avg_ptr + $avg_len / 2) % $avg_len];
		if (++$lines < $avg_len) {
			next;
		}
		print "$t $ts $as_y $a $_y\n";
	} else {
		print("unknown line $_");
		exit;
	}
}

#!/usr/bin/perl -w

use strict;

if (@ARGV != 1) {
	print("usage: cut_move.pl <file>\n");
	exit;
}


my ($file, $start, $end)  = @ARGV;

open(FH, '<', $file) or die "opening $file failed";

# 19.749500000 DATA as -13.090332 -10.634766 -0.003906 x/y -12.000000 0.029905 z 270.000000 270.000000 270.000000 e 0 delta_as -2.455566 dx_as1 1.090332 dx_as2 -1.365234 dy_as 0.033811
# 0.000000000 CMPL as 0.000000 0.000488 0.000000 x/y -12.000000 0.000000 z 270.000000 270.000000 270.000000 e 0 dro 0.000 AB -12.000000 -12.000000 -0.000977

# 19.749501083 HOME endstop 1

my @home_seq = (2, 2, 1, 1, 0, 0);
my $state = "homing";
my $homing_done = 0;

my $prev_x;
my $prev_y;
my $prev_z;
my $idle_start;
my $out_start;
my $output;
my $seq = 1;
my $x_prev = 0;
my $y_prev = 0;
my $x_prev2 = 0;
my $y_prev2 = 0;
my $ts_prev = 0;
my $ts_prev2 = 0;
my $as_x1_avg = 0;
my $as_x2_avg = 0;
my $as_y_avg = 0;
my @as_x1_ring = (0, 0, 0, 0, 0);
my @as_x2_ring = (0, 0, 0, 0, 0);
my @as_y_ring = (0, 0, 0, 0, 0);
my $as_x1_rptr = 0;
my $as_x2_rptr = 0;
my $as_y_rptr = 0;
while (<FH>) {
	next unless (/^\s*([\d.]+)\s/);

	if (/^\s*([\d.]+)\s+CMPL as (\S+) (\S+) (\S+) x.y (\S+) (\S+) z (\S+) (\S+) (\S+) e (\S+) dro (\S+) ab (\S+) (\S+) (\S+)$/) {
		my ($ts, $as_x1, $as_x2, $as_y, $x, $y, $z1, $z2, $z3, $e, $dro, $ab_x1, $ab_x2, $ab_y) =
			($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14);
		if ($state eq "wait_z") {
			# wait for z to reach <= 250
			if ($z1 < 251) {
				print("z reached\n");
				$state = "wait_idle";
				$idle_start = undef;
			}
		} elsif ($state eq "wait_idle") {
			if ($idle_start and $prev_x == $x and $prev_y == $y and $prev_z == $z1) {
				if ($1 - $idle_start > 0.2) {
					print("idle found at $1\n");
					$state = "wait_start";
					$output = undef;
				}
			} else {
				$prev_x = $x;
				$prev_y = $y;
				$prev_z = $z1;
				$idle_start = $ts;
			}
		} elsif ($state eq "wait_start") {
			if ($prev_x != $x or $prev_y != $y or $prev_z != $z1) {
				my $ofile = $file . ".part-" . $seq;
				++$seq;
				print("creating $ofile\n");
				open ($output, ">", $ofile) or
					die "failed to open part file";
				$out_start = $ts;

				$state = "wait_idle";
			}
		}
		# avg of 5 for as, delay x/y by 2 slots to get in the middle
		if (0) {
			$as_x1_avg -= $as_x1_ring[$as_x1_rptr];
			$as_x1_avg += $as_x1;
			$as_x1_ring[$as_x1_rptr] = $as_x1;
			if (++$as_x1_rptr == 5) {
				$as_x1_rptr = 0;
			}
			$as_x2_avg -= $as_x2_ring[$as_x2_rptr];
			$as_x2_avg += $as_x2;
			$as_x2_ring[$as_x2_rptr] = $as_x2;
			if (++$as_x2_rptr == 5) {
				$as_x2_rptr = 0;
			}
			$as_y_avg -= $as_y_ring[$as_y_rptr];
			$as_y_avg += $as_y;
			$as_y_ring[$as_y_rptr] = $as_y;
			if (++$as_y_rptr == 5) {
				$as_y_rptr = 0;
			}
		}
		if ($output) {
			my $t = sprintf("%8.6f", $ts - $out_start);
			my $ax1 = $as_x1;
			my $ax2 = $as_x2;
			my $ay = $as_y;
			my $dy = $y - $as_y;
			print $output "$t $ts $ax1 $ax2 $ay $x $y $dy $ab_x1 $ab_x2 $ab_y\n";
		}
		# todo only update with one of the as
		#$x_prev2 = $x_prev;
		#$y_prev2 = $y_prev;
		#$ts_prev2 = $ts_prev;
		#$x_prev = $x;
		#$y_prev = $y;
		#$ts_prev = $ts;
	} elsif (/^\s*([\d.]+)\s+HOME endstop (\d+)$/) {
		print $_;
		if (@home_seq == 0) {
			print("unexpected HOME $_");
			exit;
		}
		if ($2 != $home_seq[0]) {
			print("HOME not in sequence $_");
			exit;
		}
		shift @home_seq;
		if (@home_seq == 0) {
			print("homing done.\n");
			$state = "wait_z";
		}
	} elsif (/^\s*([\d.]+)\s+AVG/) {
		# ignore
	} else {
		print("unknown line $_");
		exit;
	}

}

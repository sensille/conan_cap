#!/usr/bin/perl -w

use strict;

if (@ARGV != 1) {
	print("usage: cut_move.pl <file>\n");
	exit;
}


my ($file, $start, $end)  = @ARGV;

open(FH, '<', $file) or die "opening $file failed";
open(MINMAX, '>', $file . ".minmax") or die "opening minmax failed";

# 19.749500000 DATA as -13.090332 -10.634766 -0.003906 x/y -12.000000 0.029905 z 270.000000 270.000000 270.000000 e 0 delta_as -2.455566 dx_as1 1.090332 dx_as2 -1.365234 dy_as 0.033811
# 19.749501083 HOME endstop 1

my @home_seq = (2, 2, 1, 1, 0, 0);
my $state = "homing";
my $homing_done = 0;

my $prev_x;
my $prev_y;
my $prev_z;
my $idle_start;
my $layer_z;
my $out_start;
my $output;
my $seq = 1;
my $max_seq = 1;
my $first_z = 0;
my $min_x;
my $min_y;
my $max_x;
my $max_y;
my $min_ax1;
my $max_ax1;
my $min_ax2;
my $max_ax2;
my $min_ay;
my $max_ay;
while (<FH>) {
	next unless (/^\s*([\d.]+)\s/);

	if (/^\s*([\d.]+)\s+CMPL as (\S+) (\S+) (\S+) x.y (\S+) (\S+) z (\S+) (\S+) (\S+) e (\S+) dro (\S+)$/) {
		my ($ts, $as_x1, $as_x2, $as_y, $x, $y, $z1, $z2, $z3, $e, $dro) =
			($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);
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
			if ($prev_x != $x or $prev_y != $y) {
				my $ofile = $file . ".part-" . $seq;
				if ($seq == 1) {
					$first_z = $z1;
				}
				++$seq;
				print("creating $ofile\n");
				open ($output, ">", $ofile) or
					die "failed to open part file";
				$state = "wait_layer";
				$layer_z = $z1;
				$out_start = $ts;
				$min_x = $x;
				$max_x = $x;
				$min_y = $y;
				$max_y = $y;
				$min_ax1 = $as_x1;
				$max_ax1 = $as_x1;
				$min_ax2 = $as_x2;
				$max_ax2 = $as_x2;
				$min_ay = $as_y;
				$max_ay = $as_y;
			}
		} elsif ($state eq "wait_layer") {
			if ($prev_x == $x and $prev_y == $y) {
				if ($z1 - $layer_z >= 0.05) {
					print("layer found at $1\n");
					$prev_z = $z1;
					$state = "wait_start";
					$output = undef;
					my $z = $z1 - $first_z;
					my $layer = $seq - 1;
					print MINMAX "layer $layer z $z x $min_x - $max_x y $min_y - $max_y ".
						"as_x1 $min_ax1 - $max_ax1 as_x2 $min_ax2 - $max_ax2 ".
						"as_y $min_ay - $max_ay\n";
				}
			} else {
				$prev_x = $x;
				$prev_y = $y;
				$layer_z = $z1;
			}
			$min_x = $x if ($x < $min_x);
			$max_x = $x if ($x > $max_x);
			$min_y = $y if ($y < $min_y);
			$max_y = $y if ($y > $max_y);
			$min_ax1 = $as_x1 if ($as_x1 < $min_ax1);
			$max_ax1 = $as_x1 if ($as_x1 > $max_ax1);
			$min_ax2 = $as_x2 if ($as_x2 < $min_ax2);
			$max_ax2 = $as_x2 if ($as_x2 > $max_ax2);
			$min_ay = $y if ($as_y < $min_ay);
			$max_ay = $y if ($as_y > $max_ay);
		}
		if ($output) {
			my $t = sprintf("%8.6f", $ts - $out_start);
			my $ax1 = $as_x1;
			my $ax2 = $as_x2;
			my $ay = $as_y;
			my $z = $z1 - $first_z;
			print $output "$t $ts $ax1 $ax2 $ay $x $y $z\n";
		}
	} elsif (/^\s*([\d.]+)\s+HOME endstop 3$/) {
		my $ts = $1;

		# still in z_tilt_adjust, reset state
		$out_start = $ts;
		$state = "wait_idle";
		if ($seq > $max_seq) {
			$max_seq = $seq;
		}
		$seq = 1;
		print "bltouch detected, reset state\n";
	} elsif (/^\s*([\d.]+)\s+HOME endstop (\d+)$/) {
		my $endstop = $2;
		print $_;
		if (@home_seq == 0) {
			print("unexpected HOME $_");
			exit;
		}
		if ($endstop != $home_seq[0]) {
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
		last;
	}
}

print "seq $seq max_seq was $max_seq\n";
while (++$seq <= $max_seq) {
	my $ofile = $file . ".part-" . $seq;
	unlink($ofile);
}

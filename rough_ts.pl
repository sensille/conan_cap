#!/usr/bin/perl -w

my $last = 0;
my $line = 0;
while (<>) {
	next unless (/^\s*([\d.]+)\s/);

	++$line;
	next if int($1 * 10) == int($last * 10);

	$last = $1;

	print "$line $_";
}

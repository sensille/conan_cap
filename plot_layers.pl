#!/usr/bin/perl -w

use strict;

my $name = $ARGV[0];
my $descr = $ARGV[1];
my $path = "/var/www/html/as";

if (!defined $name || !defined $descr) {
	die "no base filename or description given";
}

my $seq = 1;
goto html;
while (1) {
	my $fn = $name . ".data.part-" . $seq;

	if (!-e $fn) {
		last;
	}

	print "generating layer $seq\n";
	system "cp", $fn, "data";
	system "gnuplot", "layer.gp";
	system "cp", "layer.png", "$path/$name-$seq.png";
	system "cp", "layer-x12.png", "$path/$name-$seq-x12.png";
	system "cp", "layer-t.png", "$path/$name-$seq-t.png";
	system "cp", "layer-t-x12.png", "$path/$name-$seq-t-x12.png";

	++$seq;
}

html:
$seq= 97;
# generate HTMLs
open(FH, '>', $path . '/' . $name . ".html") or die;
print FH "<html><title>$name path deviation</title>\n";
print FH "<h1>$name path deviation</h1>\n";
print FH "<h2>$name printed at/with $descr</h2>\n";
for (1 .. $seq) {
	print FH "<a href=$name-$_.html>Layer $_</a><br>\n";
}
print FH "</body></html>\n";
close(FH);

for (1 .. $seq) {
	open(FH, '>', "$path/$name-$_.html") or die;
	print FH "<html><title>$name layer $_</title>\n";
	print FH "<h1>Layer $_</h1>\n";
	print FH "<h2>\n";
	if ($_ != 1) {
		print FH "<a href=$name-".($_ - 1).".html>prev</a>\n";
	}
	if ($_ != $seq) {
		print FH "<a href=$name-".($_ + 1).".html>next</a>\n";
	}
	print FH "</h2>\n";
	print FH "Images recorded on corexy with 3 linear encoders: x1, x2 and y. The 4 images are:\n";
	print FH "Images recorded on corexy with 3 linear encoders: x1, x2 and y. The 4 images are:\n";
	print FH "<ul>\n";
	print FH "<li>commanded vs. measured path (avg x, y)</li>\n";
	print FH "<li>commanded vs. measured path (x1, x2, y)</li>\n";
	print FH "<li>time vs. deviation (avg x, y)</li>\n";
	print FH "<li>time vs. deviation (x1, x2, y)</li>\n";
	print FH "</ul>\n";
	print FH "<br>\n";
	print FH "<a href=\"$name-$_.png\">\n";
	print FH "<img src=\"$name-$_.png\" width=1000 height=1000>\n";
	print FH "</a>\n";
	print FH "<a href=\"$name-$_-x12.png\">\n";
	print FH "<img src=\"$name-$_-x12.png\" width=1000 height=1000>\n";
	print FH "</a>\n";
	print FH "<a href=\"$name-$_-t.png\">\n";
	print FH "<img src=\"$name-$_-t.png\" width=1000 height=300>\n";
	print FH "</a>\n";
	print FH "<a href=\"$name-$_-t-x12.png\">\n";
	print FH "<img src=\"$name-$_-t-x12.png\" width=1000 height=300>\n";
	print FH "</a>\n";
	print FH "</body></html>\n";
	close(FH);
}

#!/bin/sh

TARGET=$1

#mv ~/*.cap caps
./conan_cap -r caps/$TARGET.cap -q1 > plot.data
./cut_moves.pl plot.data
gnuplot plot.gp
cp move.png /var/www/html/as/$TARGET.png
cp move2.png /var/www/html/as/$TARGET-t.png

set term png size 10000,3000
set output "move.png"
set xtics 2
set ytics 0.005
set grid
plot "plot.data.part-3" using 7:($7-$5) title "run3" with lines, \
     "plot.data.part-4" using 7:($7-$5) title "run4" with lines, \
     "plot.data.part-5" using 7:($7-$5) title "run1-2" with lines, \
     "plot.data.part-6" using 7:($7-$5) title "run2-2" with lines, \
     "plot.data.part-7" using 7:($7-$5) title "run3-2" with lines, \
     "plot.data.part-8" using 7:($7-$5) title "run4-2" with lines, \
     "plot.data.part-9" using 7:($7-$5) title "run4-2" with lines, \
     "plot.data.part-10" using 7:($7-$5) title "run5-2" with lines, \
\
     "plot.data.part-3" using 7:($3-$4-0.15) title "run3" with lines, \
     "plot.data.part-4" using 7:($3-$4-0.15) title "run4" with lines, \
     "plot.data.part-5" using 7:($3-$4-0.15) title "run1-2" with lines, \
     "plot.data.part-6" using 7:($3-$4-0.15) title "run2-2" with lines, \
     "plot.data.part-7" using 7:($3-$4-0.15) title "run3-2" with lines, \
     "plot.data.part-8" using 7:($3-$4-0.15) title "run4-2" with lines, \
     "plot.data.part-9" using 7:($3-$4-0.15) title "run4-2" with lines, \
     "plot.data.part-10" using 7:($3-$4-0.15) title "run5-2" with lines

set output "move2.png"
plot "plot.data.part-3" using 1:($7-$5) title "run3" with lines, \
     "plot.data.part-4" using 1:($7-$5) title "run4" with lines, \
     "plot.data.part-5" using 1:($7-$5) title "run1-2" with lines, \
     "plot.data.part-6" using 1:($7-$5) title "run2-2" with lines, \
     "plot.data.part-7" using 1:($7-$5) title "run3-2" with lines, \
     "plot.data.part-8" using 1:($7-$5) title "run4-2" with lines, \
     "plot.data.part-9" using 1:($7-$5) title "run4-2" with lines, \
     "plot.data.part-10" using 1:($7-$5) title "run5-2" with lines, \
\
     "plot.data.part-3" using 1:($3-$4-0.15) title "run3" with lines, \
     "plot.data.part-4" using 1:($3-$4-0.15) title "run4" with lines, \
     "plot.data.part-5" using 1:($3-$4-0.15) title "run1-2" with lines, \
     "plot.data.part-6" using 1:($3-$4-0.15) title "run2-2" with lines, \
     "plot.data.part-7" using 1:($3-$4-0.15) title "run3-2" with lines, \
     "plot.data.part-8" using 1:($3-$4-0.15) title "run4-2" with lines, \
     "plot.data.part-9" using 1:($3-$4-0.15) title "run4-2" with lines, \
     "plot.data.part-10" using 1:($3-$4-0.15) title "run5-2" with lines

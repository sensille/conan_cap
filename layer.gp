set term png size 8000,8000
set output "layer.png"
set xtics 2
set ytics 2
set grid
plot "data" using 6:7 title "x/y" with lines, \
     "data" using ($4 + ($5 / 250) * ($3 - $4)):5 title "as_x avg/y" with lines

set output "layer-x12.png"
set xtics 2
set ytics 2
set grid
plot "data" using 6:7 title "x/y" with lines, \
     "data" using 3:5 title "as_x1/y" with lines, \
     "data" using 4:5 title "as_x2/y" with lines

set term png size 10000,3000
set xtics 1
set ytics 0.02
set output "layer-t-x12.png"
plot "data" using 1:($3-$6) title "as_x1" with lines, \
     "data" using 1:($4-$6) title "as_x2" with lines, \
     "data" using 1:($5-$7) title "as_y" with lines

set output "layer-t.png"
plot "data" using 1:(($4 + ($5 / 250) * ($3 - $4)) - $6) title "as_x avg" with lines, \
     "data" using 1:($5-$7) title "as_y" with lines

#!/bin/sh

echo set title \"Time to untar a kernel source tree vs Number of Snapshots\"
echo set xlabel \"Number of Snapshots\"
echo set ylabel \"Real time \(s\) to untar kernel and sync\"
echo set key below
echo set terminal postscript enhanced color
echo set output \"all_tests.ps\"
echo -n "plot "
count=0
for testfile in `ls *:*k`
do
        if [ $count -eq 0 ]  ; then
                echo -n " \"$testfile\" using 1:2 w lp t \"$testfile\" "
        else
                echo -n ", \"$testfile\" using 1:2 w lp t\"$testfile\" "
        fi
        count=$(($count + 1))
done;

for testfile in `ls raw`
do
         echo -n ", \"$testfile\" using 1:2 w lp t \"$testfile\" "
done;
echo ""

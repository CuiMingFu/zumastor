#!/bin/bash

output_dir=$1

for dir in `ls $output_dir`; do
	[[ -d $output_dir/$dir ]] || continue
	rm -f $output_dir/$dir.dots
	rm -f /tmp/dots
	for subdir in `ls $output_dir/$dir`; do
		tail -n 1 $output_dir/$dir/$subdir/summary >> /tmp/dots
		sort -n /tmp/dots > $output_dir/$dir.dots
	done
done

echo set title \"Fstress benchmark\"
echo set xlabel \"NFS operations per second\"
echo set ylabel \"Average latency \(msec\)\"
echo set key below
echo set terminal postscript enhanced color
echo set output \"all_tests.ps\"
echo -n "plot "
count=0
for testfile in `ls $output_dir/*.dots`
do
	title=`basename $testfile .dots`
        if [ $count -eq 0 ]  ; then
                echo -n " \"$testfile\" using 1:2 w lp t \"$title\" "
        else
                echo -n ", \"$testfile\" using 1:2 w lp t\"$title\" "
        fi
        count=$((count + 1))
done;
echo ""


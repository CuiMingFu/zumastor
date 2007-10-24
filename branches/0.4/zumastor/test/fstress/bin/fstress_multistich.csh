#!/usr/local/bin/tcsh -f

set TMP = "/tmp/tmpstich"
set SCALE = "1.0"
set ALL = ""
set COMMA = ""

set AWK = "/does_not_exist"
if (! -e "$AWK") set AWK = `which nawk`
if (! -e "$AWK") set AWK = `which gawk`
if (! -e "$AWK") set AWK = `which awk`

mkdir temp

foreach DIR ($*) 
    set NAME = `echo $DIR | sed -e "s/output./x/"`
    if ("$ALL" != "") set COMMA = ","
    set ALL = "${ALL}${COMMA} '$NAME'"

    set FILES = "$DIR/*"
    cat $FILES | $AWK -f $FSTRESS_HOME/bin/fstress_stich.awk | \
    grep -v lat | $AWK '{print $7 " " $9}' > temp/$NAME
end

echo "load" > temp/points
foreach DIR ($*) 
    set NAME = `echo $DIR | sed -e "s/output./x/"`
    $AWK '{print $1}' < temp/$NAME >> temp/points
end
sort -n temp/points | uniq > temp/alldata
foreach DIR ($*) 
    set NAME = `echo $DIR | sed -e "s/output./x/"`
    echo "$NAME" | sed -e "s/x//" >! temp/thisdata
#   echo "$NAME" | sed -e "s/x//" -e "s/\.[0-9]//" >! temp/thisdata
#   echo "$NAME" | sed -e "s/x//" -e "s/affinity[^\.]*\.//g" >! temp/thisdata
    $AWK '{print $2}' < temp/$NAME >> temp/thisdata
    paste temp/alldata temp/thisdata > temp/alldata.2
    /bin/mv -f temp/alldata.2 temp/alldata
end

exit
cd temp

#set yrange [0:20]
#set nokey
cat >! $TMP.plot <<EOF
#set yrange [0:30]
set data style linespoints
set xlabel "desired ops/s"
set ylabel "avg latency (msec/op)"
set key off
set term postscript enhanced color
set size ${SCALE},${SCALE}
set output "$TMP.ps"
plot $ALL
quit
EOF
gnuplot $TMP.plot
/bin/rm $TMP.plot
cd ..

echo "-----"
cat temp/alldata
echo "-----"

/bin/rm temp/*
/bin/rmdir temp

gv $TMP.ps
/bin/rm $TMP.ps



#EOF

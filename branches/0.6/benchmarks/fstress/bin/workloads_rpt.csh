#!/bin/csh -f

#set WORKLOAD = "$1"
#set DIR = "$WORKLOAD"
#if (! -d $DIR) then
#    if (-d $FSTRESS_HOME/sample_workloads/$WORKLOAD) then
#	 set DIR = "$FSTRESS_HOME/sample_workloads/$WORKLOAD"
#    endif
#endif
#if (! -d $DIR) then
#    echo "no such workload directory $1"
#    exit 1
#endif

set DISTMANIP = "awk -f $FSTRESS_HOME/bin/distmanip.awk"

# -----------------------------------------------------------------------
echo "\documentclass[]{article}"
echo "\begin{document}"
# -----------------------------------------------------------------------

foreach DIR ($FSTRESS_HOME/sample_workloads/*)
set WORKLOAD = `echo $DIR | sed -e 's@.*/@@g'`
if ("$WORKLOAD" == "template") continue;

set FSIZE = `$DISTMANIP weighted_average=1 < $DIR/fsize`
set FCNT = `$DISTMANIP weighted_average=1 < $DIR/fcnt`
set DCNT = `$DISTMANIP weighted_average=1 < $DIR/dcnt`
set LCNT = `$DISTMANIP weighted_average=1 < $DIR/lcnt`
set MAXDEPTH = `cat $DIR/maxdepth`
set DTOT = `echo "$DCNT ^ ($MAXDEPTH - 1)" | bc`
set FTOT = `echo "$DTOT * $FCNT" | bc`
set LTOT = `echo "$DTOT * $LCNT" | bc`
set BTOT = `echo "$FTOT * $FSIZE" | bc`

# -----------------------------------------------------------------------
echo "\subsection{$WORKLOAD}"

echo "average fsize/file: $FSIZE \\"
echo "average fcnt/dir: $FCNT \\"
echo "average dcnt/dir: $DCNT \\"
echo "average lcnt/dir: $LCNT \\"
echo "maxdepth: $MAXDEPTH \\"
echo "expected total dirs: $DTOT \\"
echo "expected total files: $FTOT \\"
echo "expected total symlinks: $LTOT \\"
echo "expected total bytes: $BTOT"

echo "op distribution:"
echo "{\small \begin{tabular}{|c|c|} \hline"
echo "Value & Weight \\ \hline\hline"
cat $DIR/op | $DISTMANIP \
	nfsop_values=1 \
	percent_weights=1 \
    | awk '{print $1 " & " $2 "\\% \\\\ \\hline"}'
echo "\end{tabular} }"

foreach DIST (fsize fcnt fpop dcnt dpop lcnt lpop rsize wsize)
echo "$DIST distribution:"
echo "{\small \begin{tabular}{|c|c|} \hline"
echo "Value & Weight \\ \hline\hline"
cat $DIR/$DIST | $DISTMANIP \
	trunc_values=1 \
	percent_weights=1 \
    | awk '{print $1 " & " $2 "\\% \\\\ \\hline"}'
echo "\end{tabular} }"
end #foreach DIST
# -----------------------------------------------------------------------

end #foreach WORKLOAD

# -----------------------------------------------------------------------
echo "\end{document}"
# -----------------------------------------------------------------------

#EOF

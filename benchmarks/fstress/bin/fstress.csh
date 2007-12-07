#!/usr/bin/tcsh -f
# fstress script
# Darrell Anderson

#################################################################
# DEFAULT VALUES
#################################################################

set START_LOAD = 250
set END_LOAD = 1000
set INCR_LOAD = 250
set FIXFILESET = 0
set RUNNAME = ""

set RUN_ARGS = ""
set FILL_ARGS = ""

set RSH = "rsh"
set ROOTME = ""

#################################################################
# DO NOT TOUCH
#################################################################

set SAVEARGS = "$*"
set SLAVE = 0

set CLIENTS = ""
set NCLIENTS = 0
set SERVER = ""
set NSFILE = "/var/tmp/nsfile"
set QUITFILE = "/var/tmp/quitfile"
set LOGDIR = "$FSTRESS_HOME/output"

set RUN_ARGS = "$RUN_ARGS -quitfile $QUITFILE"

#################################################################
# USAGE AND ARG PARSING
#################################################################

if ($#argv == 0) then
usage:
    echo "fstress.csh"

    echo ""

    echo "   -clients C1 ... Cn"
    echo "   -server S"
    echo "   -transp X [default udp]"
    echo "   -rexmitmax N [default 2 times]"
    echo "   -rexmitage N [default 2000 ms]"
    echo "   -ssh [default is rsh]"
    echo "   -root [default is current user]"

    echo ""

    echo "   -low N [default $START_LOAD ops/s]"
    echo "   -high N [default $END_LOAD ops/s]"
    echo "   -incr N [default $INCR_LOAD ops/s]"
    echo "   -warmup N [default 300 s]"
    echo "   -run N [default 300 s]"
    echo "   -cooldown N [default 10 s]"
    echo "   -fixfileset N [default off]"
    echo "   -maxlat N [default 20 ms]"
    echo "   -maxios N [default 16 ops/file]"
    echo "   -maxinuse N [default 8192 files]"
    echo "   -maxops N [default N/A ops]"

    echo ""

    echo "   -runname X [default '$RUNNAME']"
    echo "   -rusage"
    echo "   -loop N"

    echo ""

    echo "   -workload X [default N/A]"

    echo ""

    echo "   -maxdepth N [default SPECsfs-like]"
    echo "   -fcntdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -fpopdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -fsizedist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -dcntdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -dpopdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -lcntdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -lpopdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -opdist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -rsizedist v:w,v:w,...,v:w [default SPECsfs-like]"
    echo "   -wsizedist v:w,v:w,...,v:w [default SPECsfs-like]"

    exit 1
endif

while ($#argv)
    set ARG = $argv[1]
    shift
    switch ($ARG)

    case "-clients":
	set CLIENTS = ""
	while ($#argv != 0)
	    if ("$argv[1]" =~ -*) break
	    set CLIENTS = "$CLIENTS $argv[1]"
	    @ NCLIENTS = $NCLIENTS + 1
	    shift
	end
	set RUN_ARGS = "$RUN_ARGS -nclients $NCLIENTS"
	breaksw
    case "-server":
	if ($#argv != 0) then
		set SERVER = "$argv[1]"
		shift
	endif
	breaksw

    case "-low":
	if ($#argv != 0) then
		set START_LOAD = "$argv[1]"
		shift
	endif
	breaksw
    case "-high":
	if ($#argv != 0) then
		set END_LOAD = "$argv[1]"
		shift
	endif
	breaksw
    case "-incr":
	if ($#argv != 0) then
		set INCR_LOAD = "$argv[1]"
		shift
	endif
	breaksw

    case "-warmup":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-run":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS -duration $argv[1]"
		shift
	endif
	breaksw
    case "-loop":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS -loop $argv[1]"
		shift
	endif
	breaksw
    case "-cooldown":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw

    case "-rexmitmax":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-rexmitage":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw

    case "-transp":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-maxlat":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-maxios":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-maxinuse":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-maxops":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-fixfileset":
	if ($#argv != 0) then
		set FIXFILESET = "$argv[1]"
		shift
	endif
	breaksw

    case "-ssh":
	set RSH = "ssh"
	breaksw
    case "-root":
	set ROOTME = "-l root"
	breaksw

    case "-workload":
	if ($#argv != 0) then
		set WORKLOAD = "$argv[1]"
		if (! -d $WORKLOAD) then
		    set WORKLOAD = "$FSTRESS_HOME/sample_workloads/$argv[1]"
		endif
		if (! -d $WORKLOAD) then
		    echo "no such workload $WORKLOAD"
		    exit 1
		endif
		set FILL_ARGS = "$FILL_ARGS $ARG $WORKLOAD"
		set RUN_ARGS = "$RUN_ARGS $ARG $WORKLOAD"
		shift
	endif
	breaksw

    case "-maxdepth":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endifdefault SPECsfs-like]"
	breaksw
    case "-fcntdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-fpopdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-fsizedist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-dcntdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-dpopdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-lcntdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-lpopdist":
	if ($#argv != 0) then
		set FILL_ARGS = "$FILL_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw

    case "-opdist":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-rsizedist":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw
    case "-wsizedist":
	if ($#argv != 0) then
		set RUN_ARGS = "$RUN_ARGS $ARG $argv[1]"
		shift
	endif
	breaksw

    case "-runname":
	if ($#argv != 0) then
		set LOGDIR = "$LOGDIR.$argv[1]"
		shift
	endif
	breaksw

    case "-rusage":
	set FILL_ARGS = "$RUN_ARGS $ARG"
	set RUN_ARGS = "$RUN_ARGS $ARG"
	breaksw

    default:
	echo "unknown argument '${ARG}'"
	goto usage
    endsw
end

#################################################################
# CREATE THE INITIAL FILE SET
#################################################################

foreach CLIENT ($CLIENTS)
    $RSH $ROOTME $CLIENT /bin/rm -f $QUITFILE >& /dev/null
end

if (! -d $LOGDIR) mkdir $LOGDIR

if ($FIXFILESET != 0) then
    set NSFILE_MAX = $FIXFILESET
    set START_SET = $FIXFILESET
else
    set NSFILE_MAX = $END_LOAD
    set START_SET = $START_LOAD
endif

echo "-------------------------------------------------------"
echo "CREATE INITIAL FILES"
echo "-------------------------------------------------------"

foreach CLIENT ($CLIENTS)
    set MAXLOAD = `expr $NSFILE_MAX / $NCLIENTS`
    $RSH $ROOTME $CLIENT \
    $FSTRESS_HOME/bin/fstress_init.csh -nsfile $NSFILE -nsprefix $CLIENT \
	-maxload $MAXLOAD \
	>&! $LOGDIR/log.$CLIENT &
end
wait
foreach CLIENT ($CLIENTS)
    set ADDLOAD = `expr $START_SET / $NCLIENTS`
    $RSH $ROOTME $CLIENT \
    $FSTRESS_HOME/bin/fstress_fill.csh -nsfile $NSFILE -host $SERVER \
	-addload $ADDLOAD $FILL_ARGS \
	>>& $LOGDIR/log.$CLIENT &
end
wait

#################################################################
# RUN, FILL LOOP
#################################################################

set LOAD = $START_LOAD
while ($LOAD <= $END_LOAD)

    foreach CLIENT ($CLIENTS)
	$RSH $CLIENT sync
    end

    echo "-------------------------------------------------------"
    echo "GENERATE LOAD ($LOAD)"
    echo "-------------------------------------------------------"
    foreach CLIENT ($CLIENTS)
	set RATE = `expr $LOAD / $NCLIENTS`
	$RSH $ROOTME $CLIENT \
	$FSTRESS_HOME/bin/fstress_run.csh -nsfile $NSFILE -host $SERVER \
	    -rate $RATE $RUN_ARGS \
	    >>& $LOGDIR/log.$CLIENT &
    end
    wait

    cat $LOGDIR/log.* | awk -f $FSTRESS_HOME/bin/fstress_stich.awk | \
	awk '{print $7 " " $9}' >! $LOGDIR/summary

    foreach CLIENT ($CLIENTS)
	$RSH $ROOTME $CLIENT "ls -1 /var/tmp" |& grep -q quitfile
	if ($status == 0) then
	    echo "client $CLIENT aborted, exiting"
	    exit -1
	endif
    end

    @ LOAD = $LOAD + $INCR_LOAD
    if ($FIXFILESET > 0 || $LOAD >= $END_LOAD) continue

    echo "-------------------------------------------------------"
    echo "CREATE INCR FILES"
    echo "-------------------------------------------------------"
    foreach CLIENT ($CLIENTS)
	set ADDLOAD = `expr $INCR_LOAD / $NCLIENTS` 
	$RSH $ROOTME $CLIENT \
	$FSTRESS_HOME/bin/fstress_fill.csh -nsfile $NSFILE -host $SERVER \
	    -addload $ADDLOAD $FILL_ARGS \
	    >>& $LOGDIR/log.$CLIENT &
    end
    wait

end

#################################################################
# EOF
#################################################################


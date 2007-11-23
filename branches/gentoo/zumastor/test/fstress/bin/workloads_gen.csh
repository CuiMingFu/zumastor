#!/usr/local/bin/tcsh -f

set FSTRESS_WORKLOADS = "$FSTRESS_HOME/sample_workloads"
set FSTRESS_OBJ = "$FSTRESS_HOME/obj-`uname -s`-`uname -m`"
set GEN_DIST = "$FSTRESS_OBJ/gen_dist"

# ---------------------------------------------------------------
# sanity checks
# ---------------------------------------------------------------

if (! -d $FSTRESS_HOME) then
    echo "no such FSTRESS_HOME dir $FSTRESS_HOME"
    exit 1
endif
if (! -d $FSTRESS_OBJ) then
    echo "no such FSTRESS_OBJ dir $FSTRESS_OBJ"
    echo "run make"
    exit 1
endif
if (! -e $GEN_DIST) then
    echo "no such GEN_DIST binary $GEN_DIST"
    echo "run make"
    exit 1
endif
if (-d $FSTRESS_WORKLOADS) then
    echo "workload dir $FSTRESS_WORKLOADS already exists"
    exit 1
endif
mkdir $FSTRESS_WORKLOADS
if ($status) then
    echo "mkdir $FSTRESS WORKLOADS failed"
    exit 1
endif

# ---------------------------------------------------------------
# useful constants
# ---------------------------------------------------------------

set NFSPROC_NULL = 0
set NFSPROC_GETATTR = 1
set NFSPROC_SETATTR = 2
set NFSPROC_LOOKUP = 3
set NFSPROC_ACCESS = 4
set NFSPROC_READLINK = 5
set NFSPROC_READ = 6
set NFSPROC_WRITE = 7
set NFSPROC_CREATE = 8
set NFSPROC_MKDIR = 9
set NFSPROC_SYMLINK = 10
set NFSPROC_MKNOD = 11
set NFSPROC_REMOVE = 12
set NFSPROC_RMDIR = 13
set NFSPROC_RENAME = 14
set NFSPROC_LINK = 15
set NFSPROC_READDIR = 16
set NFSPROC_READDIRPLUS = 17
set NFSPROC_FSSTAT = 18
set NFSPROC_FSINFO = 19
set NFSPROC_PATHCONF = 20
set NFSPROC_COMMIT = 21
set NFSPROC_SEQREAD = 106
set NFSPROC_SEQWRITE = 107
set NFSPROC_APPENDWRITE = 108

# ---------------------------------------------------------------
# begin workloads
# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/default"
echo "creating default"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP       0
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD      0
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR      0
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       0
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       0
$NFSPROC_COMMIT       0
EOF

cat > $WORKLOAD/fcnt << EOF
-2 100
EOF

cat > $WORKLOAD/fpop << EOF
1 100
EOF

cat > $WORKLOAD/fsize << EOF
0 100
EOF

cat > $WORKLOAD/dcnt << EOF
5 100
EOF

cat > $WORKLOAD/dpop << EOF
1 100
EOF

cat > $WORKLOAD/lcnt << EOF
0 100
EOF

cat > $WORKLOAD/lpop << EOF
1 100
EOF

cat > $WORKLOAD/maxdepth << EOF
3
EOF

cat > $WORKLOAD/rsize << EOF
8192 85
16384 8
32768 4
65536 2
131072 1
EOF

cat > $WORKLOAD/wsize << EOF
0 49
8192 36
16384 8
32768 4
65536 2
131072 1
EOF

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/specsfs97"
echo "creating specsfs97"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP      27
$NFSPROC_READ        18
$NFSPROC_WRITE        9
$NFSPROC_SEQREAD      0
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR     11
$NFSPROC_READLINK     7
$NFSPROC_READDIR      2
$NFSPROC_CREATE       1
$NFSPROC_REMOVE       1
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      1
$NFSPROC_READDIRPLUS  9
$NFSPROC_ACCESS       7
$NFSPROC_COMMIT       5
EOF

cat > $WORKLOAD/fcnt << EOF
-2 100
EOF

cat > $WORKLOAD/fpop << EOF
1000 10
1    90
EOF

cat > $WORKLOAD/fsize << EOF
1024    33
2048    21
4096    13
8192    10
16384    8
32768    5
65536    4
131072   3
262144   2
1048576  1
EOF

cat > $WORKLOAD/dcnt << EOF
20 100
EOF

cat > $WORKLOAD/dpop << EOF
1 100
EOF

cat > $WORKLOAD/lcnt << EOF
1 100
EOF

cat > $WORKLOAD/lpop << EOF
1 100
EOF

cat > $WORKLOAD/maxdepth << EOF
2
EOF

cat > $WORKLOAD/rsize << EOF
8192 85
16384 8
32768 4
65536 2
131072 1
EOF

cat > $WORKLOAD/wsize << EOF
0 49
8192 36
16384 8
32768 4
65536 2
131072 1
EOF

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/webserver"
echo "creating webserver"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP      14
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD     28
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR     55
$NFSPROC_READLINK     0
$NFSPROC_READDIR      1
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       0
EOF

cat > $WORKLOAD/fcnt << EOF
-4 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 0.6 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/fpop

$FSTRESS_OBJ/gen_dist -pareto 0.4 1024 -cnt 100 -scale 1024 -trunc \
> $WORKLOAD/fsize

cp $FSTRESS_WORKLOADS/default/dcnt $WORKLOAD/dcnt

$FSTRESS_OBJ/gen_dist -zipf 0.6 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/dpop

cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/default/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/default/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/default/wsize $WORKLOAD/wsize

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/webserver12"
echo "creating webserver12"
mkdir $WORKLOAD

cp $FSTRESS_WORKLOADS/webserver/op $WORKLOAD/op
cp $FSTRESS_WORKLOADS/webserver/fsize $WORKLOAD/fsize
cp $FSTRESS_WORKLOADS/webserver/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/webserver/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/webserver/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/webserver/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/webserver/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/webserver/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/webserver/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/webserver/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/webserver/wsize $WORKLOAD/wsize

$FSTRESS_OBJ/gen_dist -zipf 1.2 -cnt 100 -scale 100 -trunc \
>! $WORKLOAD/fpop

$FSTRESS_OBJ/gen_dist -zipf 1.2 -cnt 100 -scale 100 -trunc \
>! $WORKLOAD/dpop

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/webserver-acp"
echo "creating webserver-acp"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP      14
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD     28
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR     55
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       0
EOF

cat > $WORKLOAD/fsize << EOF
807	7
724	7
EOF

cp $FSTRESS_WORKLOADS/webserver/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/webserver/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/webserver/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/webserver/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/webserver/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/webserver/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/webserver/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/webserver/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/webserver/wsize $WORKLOAD/wsize

set WORKLOAD = "$FSTRESS_WORKLOADS/webserver-acp-ge"
echo "creating webserver-acp-ge"
mkdir $WORKLOAD

cat > $WORKLOAD/fsize << EOF
250	7

EOF

cp $FSTRESS_WORKLOADS/webserver-acp/op $WORKLOAD/op
cp $FSTRESS_WORKLOADS/webserver-acp/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/webserver-acp/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/webserver-acp/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/webserver-acp/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/webserver-acp/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/webserver-acp/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/webserver-acp/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/webserver-acp/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/webserver-acp/wsize $WORKLOAD/wsize

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/webproxy"
echo "creating webproxy"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP      14
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_GETATTR     18
$NFSPROC_SEQREAD      6
$NFSPROC_SEQWRITE    23
$NFSPROC_APPENDWRITE  0
$NFSPROC_READLINK     0
$NFSPROC_READDIR      1
$NFSPROC_CREATE      11
$NFSPROC_REMOVE      11
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       0
EOF

cp $FSTRESS_WORKLOADS/webserver/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/webserver/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/webserver/fsize $WORKLOAD/fsize
cp $FSTRESS_WORKLOADS/webserver/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/webserver/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/webserver/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/webserver/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/webserver/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/default/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/default/wsize $WORKLOAD/wsize

# ---------------------------------------------------------------

# hit "L1" = getattr
# hit "L2" = lookup, read x2
# miss = lookup, remove, create, write x2, commit

if (0) then
foreach L1 (10 30 50 70 90)
foreach L2 (10 30 50 70 90)

set WORKLOAD = "$FSTRESS_WORKLOADS/wp.$L1.$L2"
echo "creating wp.$L1.$L2"
mkdir $WORKLOAD

set L1HITRATE = $L1
set L1X = `expr 100 - $L1HITRATE`
set L2HITRATE = `expr $L1X \* $L2 / 100`
set L2X = `expr 100 - $L2`
set L2MISSRATE = `expr $L1X \* $L2X / 100`

touch $WORKLOAD/op
echo "$NFSPROC_GETATTR $L1HITRATE" >> $WORKLOAD/op
echo "$NFSPROC_LOOKUP `expr $L2HITRATE + $L2MISSRATE`" >> $WORKLOAD/op
echo "$NFSPROC_SEQREAD `expr $L2HITRATE \* 2`" >> $WORKLOAD/op
#echo "$NFSPROC_REMOVE $L2MISSRATE" >> $WORKLOAD/op
#echo "$NFSPROC_CREATE $L2MISSRATE" >> $WORKLOAD/op
echo "$NFSPROC_WRITE `expr $L2MISSRATE \* 2`" >> $WORKLOAD/op
echo "$NFSPROC_COMMIT $L2MISSRATE" >> $WORKLOAD/op
echo "$NFSPROC_READDIR 1" >> $WORKLOAD/op
echo "$NFSPROC_FSSTAT 1" >> $WORKLOAD/op
echo "$NFSPROC_ACCESS 1" >> $WORKLOAD/op

cp $FSTRESS_WORKLOADS/webserver/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/webserver/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/webserver/fsize $WORKLOAD/fsize
cp $FSTRESS_WORKLOADS/webserver/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/webserver/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/webserver/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/webserver/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/webserver/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/webserver/fsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/webserver/fsize $WORKLOAD/wsize
cp $FSTRESS_WORKLOADS/default/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/default/wsize $WORKLOAD/wsize

cat > $WORKLOAD/extraargs << EOF
-fixfileset 1000
EOF

end #L2
end #L1
endif

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/database"
echo "creating database"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP       0
$NFSPROC_READ        61
$NFSPROC_WRITE       31
$NFSPROC_SEQREAD      0
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR      3
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       0
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       4
EOF

cat > $WORKLOAD/fcnt << EOF
8 100
EOF

cat > $WORKLOAD/fpop << EOF
1 100
EOF

cat > $WORKLOAD/fsize << EOF
67108864 100
536870912 0
1073741824 0
EOF

cat > $WORKLOAD/dcnt << EOF
0 100
EOF

cat > $WORKLOAD/dpop << EOF
1 100
EOF

cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lpop

cat > $WORKLOAD/maxdepth << EOF
1
EOF

cp $FSTRESS_WORKLOADS/default/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/default/wsize $WORKLOAD/wsize

cat > $WORKLOAD/extraargs << EOF
-fixfileset 100
EOF

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/peerpeer"
echo "creating peerpeer"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP       1
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD     54
$NFSPROC_SEQWRITE    35
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR      1
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       1
$NFSPROC_REMOVE       1
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       5
EOF

cat > $WORKLOAD/fcnt << EOF
-2 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 1.2 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/fpop

cat > $WORKLOAD/fsize << EOF
262144 53
524288 50
786432 59
1048576 65
1310720 62
1572864 59
1835008 55
2097152 83
2359296 106
2621440 142
2883584 180
3145728 194
3407872 162
3670016 244
3932160 236
4194304 256
4456448 275
4718592 266
4980736 213
5242880 219
5505024 162
5767168 140
6029312 118
EOF

cat > $WORKLOAD/dcnt << EOF
2 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 1.2 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/dpop

cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lpop

cp $WORKLOAD/fsize $WORKLOAD/rsize
cp $WORKLOAD/fsize $WORKLOAD/wsize

cat > $WORKLOAD/maxdepth << EOF
2
EOF

# ---------------------------------------------------------------

if (0) then
foreach N (0 10 20 30 40 50 60 70 80 90 100)
set BINPCT = $N
set TXTPCT = `expr 100 - $N`
set WORKLOAD = "$FSTRESS_WORKLOADS/pp${BINPCT}"
echo "creating pp${BINPCT}"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP       1
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD     54
$NFSPROC_SEQWRITE    35
$NFSPROC_APPENDWRITE  0
$NFSPROC_GETATTR      1
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       5
EOF

cp $FSTRESS_WORKLOADS/peerpeer/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/peerpeer/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/peerpeer/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/peerpeer/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/peerpeer/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/peerpeer/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/peerpeer/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/peerpeer/wsize $WORKLOAD/wsize

$FSTRESS_OBJ/gen_dist -zipf 0.3 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/fpop

touch $WORKLOAD/fsize
set BINREPEAT = `expr $BINPCT / 10`
repeat $BINREPEAT cat $FSTRESS_WORKLOADS/peerpeer/fsize \
>> $WORKLOAD/fsize

set TXTWEIGHT = `expr 34 \* $TXTPCT`
$FSTRESS_OBJ/gen_dist -pareto 0.4 64 -cnt 10 -scale 1024 -trunc \
-weight $TXTWEIGHT \
>> $WORKLOAD/fsize

cat > $WORKLOAD/extraargs << EOF
-fixfileset 500
EOF

end
endif

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/mail"
echo "creating mail"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP      27
$NFSPROC_READ         0
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD     14
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE 24
$NFSPROC_GETATTR      3
$NFSPROC_READLINK     0
$NFSPROC_READDIR      0
$NFSPROC_CREATE       0
$NFSPROC_REMOVE       0
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      4
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       3
$NFSPROC_COMMIT      24
EOF

cat > $WORKLOAD/fcnt << EOF
-2 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 1.3 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/fpop

$FSTRESS_OBJ/gen_dist -pareto 0.4 1024 -cnt 100 -scale 1024 -trunc \
> $WORKLOAD/fsize

cat > $WORKLOAD/dcnt << EOF
0 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 1.3 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/dpop

cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/default/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/default/wsize $WORKLOAD/wsize

cat > $WORKLOAD/maxdepth << EOF
2
EOF

# ---------------------------------------------------------------

set WORKLOAD = "$FSTRESS_WORKLOADS/news"
echo "creating news"
mkdir $WORKLOAD

cat > $WORKLOAD/op << EOF
$NFSPROC_LOOKUP       1
$NFSPROC_READ        22
$NFSPROC_WRITE        0
$NFSPROC_SEQREAD      0
$NFSPROC_SEQWRITE     0
$NFSPROC_APPENDWRITE 64
$NFSPROC_GETATTR      0
$NFSPROC_READLINK     0
$NFSPROC_READDIR      1
$NFSPROC_CREATE       1
$NFSPROC_REMOVE       1
$NFSPROC_MKDIR        0
$NFSPROC_RMDIR        0
$NFSPROC_FSSTAT       1
$NFSPROC_SETATTR      0
$NFSPROC_READDIRPLUS  0
$NFSPROC_ACCESS       1
$NFSPROC_COMMIT       8
EOF

cat > $WORKLOAD/fcnt << EOF
-1 100
EOF

$FSTRESS_OBJ/gen_dist -zipf 0.3 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/fpop

$FSTRESS_OBJ/gen_dist -pareto 0.4 64 -cnt 10 -scale 1024 -trunc \
> $WORKLOAD/fsize
$FSTRESS_OBJ/gen_dist -pareto 0.4 4096 -cnt 10 -scale 1024 -trunc \
>> $WORKLOAD/fsize

cat > $WORKLOAD/dcnt << EOF
1 20
2 20
3 20
4 20
EOF

$FSTRESS_OBJ/gen_dist -zipf 0.3 -cnt 100 -scale 100 -trunc \
> $WORKLOAD/dpop

cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/default/lcnt $WORKLOAD/lpop

$FSTRESS_OBJ/gen_dist -pareto 0.4 4096 -cnt 10 -scale 1024 -trunc \
>> $WORKLOAD/rsize
cp $WORKLOAD/fsize $WORKLOAD/wsize

cat > $WORKLOAD/maxdepth << EOF
3
EOF

# ---------------------------------------------------------------

if (0) then
foreach N (0 10 20 30 40 50 60 70 80 90 100)
set BINPCT = $N
set TXTPCT = `expr 100 - $N`
set WORKLOAD = "$FSTRESS_WORKLOADS/news${BINPCT}"
echo "creating news${BINPCT}"
mkdir $WORKLOAD

cp $FSTRESS_WORKLOADS/news/op $WORKLOAD/op
cp $FSTRESS_WORKLOADS/news/fcnt $WORKLOAD/fcnt
cp $FSTRESS_WORKLOADS/news/fpop $WORKLOAD/fpop
cp $FSTRESS_WORKLOADS/news/dcnt $WORKLOAD/dcnt
cp $FSTRESS_WORKLOADS/news/dpop $WORKLOAD/dpop
cp $FSTRESS_WORKLOADS/news/lcnt $WORKLOAD/lcnt
cp $FSTRESS_WORKLOADS/news/lpop $WORKLOAD/lpop
cp $FSTRESS_WORKLOADS/news/maxdepth $WORKLOAD/maxdepth
cp $FSTRESS_WORKLOADS/news/rsize $WORKLOAD/rsize
cp $FSTRESS_WORKLOADS/news/wsize $WORKLOAD/wsize

$FSTRESS_OBJ/gen_dist -pareto 0.4 64 -cnt 10 -scale 1024 -trunc \
-weight $TXTPCT \
> $WORKLOAD/fsize
$FSTRESS_OBJ/gen_dist -pareto 0.4 4096 -cnt 10 -scale 1024 -trunc \
-weight $BINPCT \
>> $WORKLOAD/fsize

end
endif

# ---------------------------------------------------------------
# simplify distributions
# ---------------------------------------------------------------

set TMPFILE = "$FSTRESS_WORKLOADS/simplify_tmpfile"

foreach WORKLOAD ($FSTRESS_WORKLOADS/*)
echo "simplifying distributions ($WORKLOAD)"

foreach DIST ($WORKLOAD/fpop $WORKLOAD/dpop $WORKLOAD/lpop)
    awk -f $FSTRESS_HOME/bin/distmanip.awk \
	trunc_values=1 \
	trunc_weights=1 \
	aggregate_values=1 \
	normalize_values=1 \
	gather_values=10 \
	< $DIST | sort -r -n > $TMPFILE
    mv $DIST $DIST.orig
    mv $TMPFILE $DIST
end

foreach DIST ($WORKLOAD/fcnt $WORKLOAD/dcnt $WORKLOAD/lcnt)
    awk -f $FSTRESS_HOME/bin/distmanip.awk \
	trunc_values=1 \
	trunc_weights=1 \
	aggregate_values=1 \
	< $DIST | sort -r -n > $TMPFILE
    mv $DIST $DIST.orig
    mv $TMPFILE $DIST
end

foreach DIST ($WORKLOAD/fsize $WORKLOAD/rsize $WORKLOAD/wsize)
    awk -f $FSTRESS_HOME/bin/distmanip.awk \
	trunc_values=1 \
	trunc_weights=1 \
	aggregate_values=1 \
	gather_values=10 \
	< $DIST | sort -r -n > $TMPFILE
    mv $DIST $DIST.orig
    mv $TMPFILE $DIST
end

end

# ---------------------------------------------------------------
# EOF
# ---------------------------------------------------------------

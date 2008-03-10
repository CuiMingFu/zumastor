#!/bin/sh
# Simple benchmark to compare time-to-take-snapshot between
# Zumastor, rsync, and lvm snapshot+rsync.

abort()
{
	echo "failed" $1
	exec false
	echo notreached
}

# Create $ndirs directories, 
#   each containing $nfiles files,
#      each containing $nkB pseudorandom kilobytes
gendata() {
    local ndirs=$1
    local nfiles=$2
    local nkB=$3
    echo gendata ndirs $ndirs nfiles $nfiles nkB $nkB
    ssh -n root@$master 'rm -rf testdata; mkdir testdata'

    d=0
    while test $d -lt $ndirs
    do
	dirname=dir$d

	ssh -n root@$master "cd testdata; mkdir $dirname; cd $dirname; \
	 seq 1 $nfiles | \
          xargs -I TOKEN dd if=/dev/urandom of=fileTOKEN.dat bs=1k count=$nkB > /dev/null 2>&1" || abort "gendata failed"

	cd ..
	d=`expr $d + 1`
    done

    ssh -n root@$master 'sync; du testdata'
}

repdemo() {
    set -x
    # Usage: sh repdemo.sh volume-size-in-MB
    local size=50
    if test x$1 != x
    then
	size=$1
    fi

    rm -f /tmp/restart-test.sh
    cat > /tmp/restart-test.sh <<_EOF_
        set -x
	# Clean up after last run, if any.
	zumastor forget volume zumatest 
	lvremove -f sysvg/test 
	lvremove -f sysvg/test_snap
	# Clear logs.  Wouldn't do this in production!  Just for testing.
	/etc/init.d/zumastor stop 
	rm -rf /var/log/zumastor /var/run/zumastor
	/etc/init.d/zumastor start
	lvcreate --name test --size ${size}M sysvg
	lvcreate --name test_snap --size ${size}M sysvg
	dd if=/dev/zero of=/dev/sysvg/test bs=${size}k count=1024
_EOF_
    for host in $master $slave
    do
	scp /tmp/restart-test.sh root@${host}:
	ssh -n root@${host} sh restart-test.sh
    done
    ssh -n root@$master "mkfs.ext3 /dev/sysvg/test ; \
     zumastor define volume zumatest /dev/sysvg/test /dev/sysvg/test_snap --initialize ; \
     zumastor define master zumatest ; \
     zumastor define target zumatest $slave"
    ssh -n root@$slave " \
      zumastor define volume zumatest /dev/sysvg/test /dev/sysvg/test_snap --initialize ; \
      zumastor define source zumatest $master "
}

# Simple latency test
# Measures how long replication takes on 1st and 2nd replication cycles 
# using two methods, zumastor and rsync
latbench() {
    local volsize=$1
    local ndirs=$2
    local nfiles=$3
    local nkB=$4
    echo volsize $volsize ndirs $ndirs nkB $nkB

    # generate desired amount of test data
    gendata $ndirs $nfiles $nkB

    # clean up old zumastor instances, start new volumes
    repdemo $volsize 

    # copy test data and then sync
    ssh -n root@$master "cp -a /root/testdata /var/run/zumastor/mount/zumatest/testdata && sync" || abort "copy failed"

    echo "#### volsize $volsize, $ndirs dirs, $nkB kB/file, first replication"
    ssh -n root@$master time zumastor replicate zumatest $slave --wait
    echo Second replication 
    ssh -n root@$master touch /var/run/zumastor/mount/zumatest/testdata/blort
    ssh -n root@$master time zumastor replicate zumatest $slave --wait

    # Create small filesystem just for the origin
    ssh -n root@$master "umount /mnt/lvmtest; \
umount /mnt/lvmtest_backup; \
lvremove -f sysvg/lvmtest_backup; \
lvremove -f sysvg/lvmtest ; \
lvcreate --name lvmtest --size ${volsize}M sysvg ; \
mkfs.ext3 /dev/sysvg/lvmtest ; \
mkdir -p /mnt/lvmtest; \
mount /dev/sysvg/lvmtest /mnt/lvmtest"

    # Populate it
    ssh -n root@$master "cp -a /root/testdata /mnt/lvmtest/testdata && sync" || abort "copy failed"

    echo "First replication with rsync"
    ssh -n root@$slave "rm -rf testdata"
    ssh -n root@$master "time rsync -r /mnt/lvmtest/testdata $slave:testdata"
    echo "Second replication with rsync"
    ssh -n root@$master "time rsync -r /mnt/lvmtest/testdata $slave:testdata"

    echo "First replication with lvm snapshot and rsync"
    ssh -n root@$slave "rm -rf testdata"

    # Time how long it takes to make the snapshot and do the rsync
    # Include time to remove the snapshot, since snapshots are expensive in lvm
    ssh -n root@$master "umount /mnt/lvmtest_backup;
lvremove -f sysvg/lvmtest_backup; \
mkdir -p /mnt/lvmtest_backup; modprobe dm-snapshot; \
time (lvcreate --name lvmtest_backup --size ${volsize}M --snapshot sysvg/lvmtest && \
mount /dev/sysvg/lvmtest_backup /mnt/lvmtest_backup && \
rsync -r /mnt/lvmtest_backup $slave:testdata; \
umount /mnt/lvmtest_backup; \
lvremove -f sysvg/lvmtest_backup )"

    echo "Second replication with lvm snapshot and rsync"
    ssh -n root@$master "time (lvcreate --name lvmtest_backup --size ${volsize}M --snapshot sysvg/lvmtest && \
mount /dev/sysvg/lvmtest_backup /mnt/lvmtest_backup && \
rsync -r /mnt/lvmtest_backup $slave:testdata; \
umount /mnt/lvmtest_backup; \
lvremove -f sysvg/lvmtest_backup )"

}

set -x

# Hostnames for master and slave.  Edit or override these.
master=${master:-vm1}
slave=${slave:-vm2}

test $master != $slave || abort "can't run with same machine as master and slave"

# Make sure no other instance is running
test -f /tmp/replatbench.lock && abort "lock file /tmp/replatbench.lock exists, are you running this test already?"
touch /tmp/replatbench.lock

# Run the benchmark once for each desired combination of parameters
# Small files
latbench 100    1 1000 1
latbench 100   10 1000 1
latbench 1000  10 1000 1
latbench 1000 100 1000 1
# Large files
latbench 100    1 1 1000
latbench 100   10 1 1000
latbench 1000 100 1 1000

# Get cpu and memory info for master and slave
ssh -n root@$master 'cat /proc/cpuinfo; free'
ssh -n root@$slave 'cat /proc/cpuinfo; free'

rm -f /tmp/replatbench.lock
echo "Done."

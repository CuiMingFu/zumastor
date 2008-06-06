#!/bin/sh 

set -u # let make sure we use initialized variable 

AGENT_LOG=/tmp/agent.log
SERVER_LOG=/tmp/server.log
AGENT_PIPE=/tmp/control
SERVER_PIPE=/tmp/server

. ./config.sh

global_test_num=0

setup_tar() {
	mkfs.ext3 $TAR_DEV || { echo "unable to mkfs tar device"; exit 1; }
	mount_dir $TAR_DEV $TAR_MNT
	cp ${SOURCE_TAR}/$KERNEL_TAR ${TAR_MNT}/
	umount_dir $TAR_MNT
}

check_environment() {
	[ -d $ORIGIN_MOUNT ] || mkdir -p $ORIGIN_MOUNT || { echo "$ORIGIN_MOUNT doesn't exists"; exit 1; }
	[ -d $TAR_MNT ] || mkdir -p $TAR_MNT || { echo "$TAR_MNT doesn't exists"; exit 1; }
        [ -e ${SOURCE_TAR}/$KERNEL_TAR ] || { cd $SOURCE_TAR; wget http://www.kernel.org/pub/linux/kernel/v2.6/${KERNEL_TAR}.bz2; bunzip2 ${KERNEL_TAR}.bz2; cd $SCRIPT_HOME; }
	[ -e $ORIG_DEV ] || { echo "$ORIG_DEV doesn't exist"; exit 1; }
	[ -e $SNAP_DEV ] || { echo "$SNAP_DEV doesn't exist"; exit 1; }
	[ -e $TAR_DEV ] || { echo "$TAR_DEV doesn't exist"; exit 1; }
	[ -d $TEST_ROOT_DIR ] || mkdir -p ${TEST_ROOT_DIR} || { echo "unable to mkdir $TEST_ROOT_DIR"; exit 1; }
}
    
# take in origin device, snapshot device, and optionally metadevice
# setup_ddsnap(origin, snapshot_store, chunksize, [meta device], [block size])
setup_ddsnap() {
        # the number of operands $# needs to be greater than or equal to 3
        [ $# -ge 1 ] && [ $# -le 3 ] || { echo "$0 setup_ddsnap: wrong number of arguments."; exit 1; }

        #makes local variables out of the inputs from the command line, first thing entered is origin
        local    orig=$ORIG_DEV
        local    snap=$SNAP_DEV
        local    csize=$1
        local    meta=""
        local    bsize=""

        if [ $# -eq 2 ]; then
                meta=$2
        elif [ $# -eq 3 ]; then
                meta=$2
                bsize=$3
        fi

        # sets chunk size (snapshot storage data) and block size (metadata size)
        copt="-c $csize"

        if [ $# -eq 3 ]; then
                bopt="-b $bsize"
        else
	        bopt=""
        fi

        [ -n $bsize ] || bsize=""

        kill_ddsnap
	echo "Initializing"
	ddsnap initialize -y $copt $bopt $snap $orig $meta 2>&1 || { echo "init failed"; exit 1; }
	ddsnap agent $AGENT_PIPE
	ddsnap server $snap $orig $meta $AGENT_PIPE $SERVER_PIPE --logfile $SERVER_LOG
	echo "Creating Device, $VOLUME_NAME"
	create_device -1 || { echo create failed; return 1; }
}


remove_devices() {
	# not sure if we should exit here
        dmsetup ls | grep $VOLUME_NAME | awk '{print $1}' | xargs -i dmsetup remove {} || { echo "unable to remove devices from dmsetup"; }
}

kill_ddsnap() {
	remove_devices
	pkill -f ddsnap
}


# mount will take the device, directory and optionally mount options
mount_dir() {
        local cmd_options=""
        if [ $# -eq 3 ]; then
                cmd_options="-o $3"
        fi       
        mount $cmd_options $1 $2 || { echo "unable to mount directory $2 (dev: $1)"; exit 1; }
}

umount_dir() {
         umount $1 || { echo "unable to umount directory $2 )"; exit 1; }
}

new_snapshot() {
        local    snapid=$1
        ddsnap create $SERVER_PIPE $snapid || { echo "unable to create snapshot id $snapid"; exit 1; }
}

run_tests() {
        local    num_tests=$1
        local    testname=$2
	local    testdir=${TEST_ROOT_DIR}/Config$global_test_num
        local    kern_tarball=${TAR_MNT}/${KERNEL_TAR} 
        local    temp=raw
        mkdir -p ${testdir} || { echo "unable to create $testdir"; exit 1; }
	touch ${testdir}/$testname || { echo "unable to create file $testname"; exit 1; }
	local    kernel_dir=$(echo $KERNEL_TAR | sed -e 's/\.tar//g')

	local count=1
	local device=""
	
	if [ $2 = raw ]; then
	        device=$RAW_DEV
	else
                device=/dev/mapper/$VOLUME_NAME
        fi    
        
        while [ $count -le $num_tests ] 
        do
        	echo "Mounting origin volume $device on  ${ORIGIN_MOUNT}"         
        	mount_dir $device ${ORIGIN_MOUNT} # no options here
        	cd ${ORIGIN_MOUNT}
                echo "Mounting $TAR_DEV on $TAR_MNT"
                mount_dir ${TAR_DEV} ${TAR_MNT} 
		echo "Warming up the cache"
                cat $kern_tarball  > /dev/null || { echo "unable to cat tarball to dev/null"; exit  1; }
                echo "Running tar xf"
                /usr/bin/time -po $testdir/runtest$count tar xf $kern_tarball
                echo "Unmounting ${TAR_MNT}"
                umount_dir ${TAR_MNT}
		mv $kernel_dir ${kernel_dir}$count	
		echo "Syncing data to disk"
		/usr/bin/time -apo $testdir/runtest$count sync
        	cd 
        	echo "Unmounting origin volume ${ORIGIN_MOUNT} from $device"
        	/usr/bin/time -po $testdir/umount$count umount ${ORIGIN_MOUNT} 
		if [ $2 != raw ]; then
		        new_snapshot $count
		fi
                count=$(($count + 1))
        done
	global_test_num=$(($global_test_num + 1))
}


mkfs_test() {
	mkdir -p $1
        /usr/bin/time -po ${1}/mkfs.test mkfs.ext3 /dev/mapper/${VOLUME_NAME} || { echo "make failed"; exit 1; }
}

create_device() {
        local    snapid=$1
        local    server=$SERVER_PIPE
        local    size=$(ddsnap status $server --size) || { echo "$0: FUNCNAME[@]} size"; exit 1; }
        echo 0 $size ddsnap $SNAP_DEV $ORIG_DEV $AGENT_PIPE $snapid | dmsetup create $VOLUME_NAME || { echo "unable to create vol device"; exit 1; }
}

# runs the specified number of tests on a raw device
# $1 = number of tests to run,
# $2 = testname)
run_raw_configuration() {
        mkfs.ext3 $RAW_DEV
        echo "Starting to run $1 raw device tests"
        run_tests $1 $2
        echo "Removing raw device ${RAW_DEV}"
        remove_devices ${RAW_DEV}
}

# runs a specific configuration
# $1 = name of test, 
# $2 = chunksize, 
# $3 = nvram or empty string if running without nvram,
# $4 = block size, 
# $5 = number of tests to run
run_configuration() {
        local    num_tests=$5
	local    testdir=${TEST_ROOT_DIR}/Config$global_test_num

	#first, the tests are run on a raw device
	if [ $global_test_num -eq 0 ] ; then
	    run_raw_configuration $num_tests "raw"
	fi

	echo "Setting up ddsnap"
        setup_ddsnap $2 $3 $4
#       new_snapshot 0
        mkfs_test $testdir # output mkfs info into testdir```
        echo "Starting to run tests"
        run_tests $num_tests $1
        echo "Removing device ${VOLUME_NAME}"
        remove_devices ${VOLUME_NAME}
        kill_ddsnap
}

plot_data() {
        cd $TEST_ROOT_DIR
        echo "Extracting data."
        $SCRIPT_HOME/extract_data.pl
        echo "Plotting graphs with gnuplot."
        $SCRIPT_HOME/generate_all_gnuplot.sh | gnuplot
        echo "Your tests have completed and your .ps graph is available in your $PWD directory"
}

# sets the x coordinate of the key for gnuplot based on the number
# of tests that were run
find_X_coord() {
        local    num_tests=$1
        local percent=94
        X_COORD_PLOT_KEY=$(($num_tests * $percent))
        X_COORD_PLOT_KEY=$(($X_COORD_PLOT_KEY / 100))
}

# runs each configuration
run_all_configurations() {

        local    num_tests=3
#	run_configuration "native:normal:4k" 4k "" "" $num_tests
#	run_configuration "native:normal:16k" 16k "" "" $num_tests
#	run_configuration "native:normal:64k" 64k "" ""	$num_tests
	run_configuration "native:normal:128k" 128k "" "" $num_tests
	run_configuration "native:normal:256k" 256k "" "" $num_tests
#       run_configuration "native:nvram:4k" 4k $META_DEV 4k $num_tests
#       run_configuration "native:nvram:16k" 16k $META_DEV 4k $num_tests
#       run_configuration "native:nvram:64k" 64k $META_DEV 4k $num_tests
#       run_configuration "native:nvram:128k" 128k $META_DEV 4k $num_tests
#       run_configuration "native:nvram:256k" 256k $META_DEV 4k $num_tests
	# setting the x coordinate of the key for the gnuplot
	find_X_coord $num_tests
}

check_environment
setup_tar
run_all_configurations
plot_data

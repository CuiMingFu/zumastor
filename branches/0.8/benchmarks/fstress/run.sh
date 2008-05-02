#!/bin/bash
# example script for comparing nfs performances of nfs-only (without zumastor),
# nfs-zumastor (with zumastor and zero snapshot), and nfs-snapshot (with zumastor and one snapshot)

# please change the testing nfs loads according to the server capacity
LOADS="200 400 600 800 1000"

# type of tests
TESTS="nfs-only nfs-zumastor nfs-snapshot"

# name of nfs server
server="server1"

# nfs server devices, please change them according to your server setting
orig_dev=/dev/sysvg/vol-10G
snap_dev=/dev/sysvg/vol-20G
meta_dev=/dev/sysvg/vol-1G
raw_dev=/dev/sda10

FSTRESS_HOME=$PWD
OUTPUT_DIR=fstress-output

function start_nfs {
	type=$1
	if [[ $type == "nfs-only" ]]; then
		ssh $server "mkdir -p /var/run/zumastor/mount/test/"
		ssh $server "mkfs.ext3 $raw_dev"
		ssh $server "mount $raw_dev /var/run/zumastor/mount/test/"
	else
		ssh $server "zumastor define volume -i test $orig_dev $snap_dev -m $meta_dev"
		ssh $server "mkfs.ext3 /dev/mapper/test"
		ssh $server "zumastor define master test"
		[[ $type == "nfs-snapshot" ]] && ssh $server "zumastor snapshot test"
	fi
	ssh $server "chmod a+w /var/run/zumastor/mount/test/"
	ssh $server "/etc/init.d/nfs-kernel-server start"
}

function stop_nfs {
	ssh $server "/etc/init.d/nfs-kernel-server stop"
	if [[ $type == "nfs-only" ]]; then
		ssh $server "rm -rf /var/run/zumastor/mount/test/*"
		ssh $server "umount /var/run/zumastor/mount/test/"
	else
		ssh $server "zumastor forget volume test"
	fi
}

# set up fstress benchmark
grep FSTRESS_HOME ~/.bashrc >& /dev/null || echo "export FSTRESS_HOME=$FSTRESS_HOME=" >> ~/.bashrc 
grep FSTRESS_HOME ~/.ssh/environment >& /dev/null || echo "FSTRESS_HOME=$FSTRESS_HOME=" >> ~/.ssh/environment 
[[ -e $FSTRESS_HOME/obj-Linux-i686 ]] || { export FSTRESS_HOME=$FSTRESS_HOME; pushd $FSTRESS_HOME; make; popd; }

# nfs client need to be able to ssh to the server without passwd
[[ -e ~/.ssh/id_dsa.pub ]] || ssh-keygen -t dsa -f ~/.ssh/id_dsa -P ''
ssh localhost "/bin/true" || cat ~/.ssh/id_dsa.pub >> ~/.ssh/authorized_keys
if ! ssh $server "/bin/true"; then
	echo "set up ssh access to server $server"
	scp ~/.ssh/id_dsa.pub root@$server:/root/.ssh/$hostname.pub
	ssh $server "cat /root/.ssh/$hostname.pub >> /root/.ssh/authorized_keys"
fi
ssh $server "grep '/var/run/zumastor/mount/test' /etc/exports" || ssh $server "echo '/var/run/zumastor/mount/test *(rw,insecure,nohide,no_subtree_check,sync)' >> /etc/exports"

# run tests
mkdir -p $OUTPUT_DIR
for ntest in $TESTS; do
        echo "start $ntest test ..."
        for load in $LOADS; do
		start_nfs $ntest
		echo "start load $load"
                $FSTRESS_HOME/bin/fstress.csh -low $load -high $load -maxlat 1000 -ssh -clients localhost -server $server:/var/run/zumastor/mount/test
		stop_nfs $ntest
                mkdir -p $OUTPUT_DIR/$ntest
		[[ -e $OUTPUT_DIR/$ntest/output-$load ]] && rm -rf $OUTPUT_DIR/$ntest/output-$load
                cp -r output $OUTPUT_DIR/$ntest/output-$load
                sleep 5
        done
done

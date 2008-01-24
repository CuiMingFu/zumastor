#!/bin/bash
#
# Copy a large volume to an exported zumastor volume via nfs, with hourly 
# replication running between the zumastor-nfs server and a backup server.
#
# Copyright 2007 Google Inc. All rights reserved.
# Author: Jiaying Zhang (jiayingz@google.com)
set -e

# change the configurations according to the machine setup
source="host1.debian.org"
target="host2.debian.org"
target_port=11235
vol="software"
source_orgdev="/dev/sysvg/vol-1G"
source_snapdev="/dev/sysvg/vol-2G"
target_orgdev="/dev/sysvg2/vol-3G"
target_snapdev="/dev/sysvg2/vol-10G"
copy_path="/terabyte-volume"
notifyemail="zumastor@debian.org"

install_packages="true"
reboot_after_install="true" # set this to false if you want to check grub and reboot manually
version="0.6"
SSH_OPTS="-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -q -l root"
SCP_OPTS="-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -q"

function ssh_setup {
	local -r host=$1
	if ssh $SSH_OPTS $host "/bin/true"; then
		echo "set up ssh access to host $host"
		scp $SCP_OPTS ~/.ssh/id_dsa.pub root@$host:/tmp/
		ssh $SSH_OPTS $host "cat /tmp/id_dsa.pub >> /root/.ssh/authorized_keys"
	fi
}

function remote_ssh_setup {
	local -r source=$1
	local -r target=$2
	if ! ssh $SSH_OPTS $source "ssh $SSH_OPTS $target /bin/true"; then
		ssh $SSH_OPTS $source "ls /root/.ssh/id_dsa.pub" || ssh $SSH_OPTS $source "ssh-keygen -t dsa -f /root/.ssh/id_dsa -P ''"
		scp $SCP_OPTS root@$source:/root/.ssh/id_dsa.pub ~/.ssh/$source.pub
		scp $SCP_OPTS ~/.ssh/$source.pub root@$target:/root/.ssh/
		ssh $SSH_OPTS $target "cat /root/.ssh/$source.pub >> /root/.ssh/authorized_keys"
	fi
}

function zuma_install {
	local -r version=$1
	local -r source=$2
	local -r target=$3
	local revision

	rm -rf zumabuild
	mkdir zumabuild
	cd zumabuild
	wget http://zumabuild/trunk/buildrev
	read revision < buildrev
	echo version $version revision $revision
	wget http://zumabuild/trunk/r$revision/ddsnap_${version}-r${revision}_i386.deb
	wget http://zumabuild/trunk/r$revision/zumastor_${version}-r${revision}_i386.deb
	wget http://zumabuild/trunk/kernel-headers-build_i386.deb
	wget http://zumabuild/trunk/kernel-image-build_i386.deb
	cd ..
	ssh $SSH_OPTS $source "rm -rf /root/zumabuild"
	scp $SCP_OPTS -r zumabuild root@$source:/root/
	ssh $SSH_OPTS $source "dpkg -i --force-confnew /root/zumabuild/*.deb" || { echo "fail to install zumastor packages on host $source"; exit 1; }
	ssh $SSH_OPTS $target "rm -rf /root/zumabuild"
	scp $SCP_OPTS -r zumabuild root@$target:/root/
	ssh $SSH_OPTS $target "dpkg -i --force-confnew /root/zumabuild/*.deb" || { echo "fail to install zumastor packages on host $target"; exit 1; }
}

function check_ready {
	local -r host=$1
	local revision version
	ssh_ready="false"
	for i in `seq 10`; do
		 if ssh $SSH_OPTS $host "/bin/true"; then
			ssh_ready="true"
			break;
		fi
		sleep 20
	done
	[[ $ssh_ready == "false" ]] && return 1

	read revision < zumabuild/buildrev
	version=`ssh $SSH_OPTS $host "zumastor --version" | awk '{ print $3 }'`
	[[ $version = $revision ]] || { echo "fail to install zumastor-$revision package"; exit 1; }
	version=`ssh $SSH_OPTS $host "ddsnap --version" |  head -n 1 | cut -d "\"" -f 2`
	[[ $version = $revision ]] || { echo "fail to install ddsnap-$revision package"; exit 1; }
	revision=`dpkg -I zumabuild/kernel-image-build_i386.deb | awk '/Description:/ { print $8 }'`
	revision=${revision/%./}
	version=`ssh $SSH_OPTS $host "uname -r"`
	[[ $version = $revision ]] || { echo "fail to install kernel-image-$revision package"; exit 1; }

	return 0
}

function monitor_replication {
	local -r vol=$1
	local smodify tmodify scurrent tcurrent shangtime thangtime

	if ssh $SSH_OPTS $source "ls /var/lib/zumastor/volumes/$vol/targets/$target/send >& /dev/null"; then
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$vol/targets/$target/send"`
	elif ssh $SSH_OPTS $source "ls /var/lib/zumastor/volumes/$vol/targets/$target/hold >& /dev/null"; then
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$vol/targets/$target/hold"`
	else
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$vol/targets/$target"`
	fi

	if ssh $SSH_OPTS $target "ls /var/lib/zumastor/volumes/$vol/source/apply >& /dev/null"; then
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$vol/source/apply"`
	elif ssh $SSH_OPTS $target "ls /var/lib/zumastor/volumes/$vol/source/hold >& /dev/null"; then
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$vol/source/hold"`
	else
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$vol/source"`
	fi

	scurrent=`ssh $SSH_OPTS $source "date +%s"`
	tcurrent=`ssh $SSH_OPTS $target "date +%s"`
	shangtime=$(( scurrent - smodify ))
	thangtime=$(( tcurrent - tmodify ))
	if [[ $shangtime -ge 14400 || $thangtime -ge 14400 ]]; then
		echo "replication has stopped for four hours"
		echo | mail -s 'ZUMASTOR LARGE VOLUME COPY TEST HANGING' $notifyemail
		exit 1
	fi

	df -h | grep zsoftware
	pgrep rsync >& /dev/null || { echo | mail -s 'ZUMASTOR LARGE VOLUME COPY TEST FINISHED' $notifyemail ; exit 0; }
}

case $1 in
install)
# initial ssh setup, requires user interaction
[[ -e ~/.ssh/id_dsa.pub ]] || ssh-keygen -t dsa -f ~/.ssh/id_dsa -P ''
ssh_setup $source
ssh_setup $target
remote_ssh_setup $source $target
remote_ssh_setup $target $source

# install/upgrade zumastor packages
[[ $install_packages == "true" ]] && zuma_install $version $source $target
if [[ $reboot_after_install == "true" ]]; then
	ssh $SSH_OPTS $source "reboot"
	ssh $SSH_OPTS $target "reboot"
	sleep 30
	check_ready $source || { echo "fail to reboot host $source"; exit 1; }
	check_ready $target || { echo "fail to reboot host $target"; exit 1; }
fi
;;

start)
# zumastor volume/replication setup and the initial replication cycle
ssh $SSH_OPTS $source "zumastor define volume -i $vol $source_orgdev $source_snapdev"
ssh $SSH_OPTS $source "mkfs.ext3 /dev/mapper/$vol"
ssh $SSH_OPTS $source "zumastor define master $vol -h 24 -d 7"
ssh $SSH_OPTS $target "zumastor define volume -i $vol $target_orgdev $target_snapdev"
ssh $SSH_OPTS $target "zumastor define source $vol $source -p 3600"
ssh $SSH_OPTS $source "zumastor define target $vol $target:$target_port -p 3600"

# set up nfs access
ssh $SSH_OPTS $source "exportfs -o'rw,fsid=1000,crossmnt,nohide' *:/var/run/zumastor/mount/$vol"
ssh $SSH_OPTS $source "chmod a+rw /var/run/zumastor/mount/$vol"
[[ -e /zsoftware ]] || { sudo mkdir /zsoftware; sudo chmod a+rwx /zsoftware; }
sudo mount -tnfs $source:/var/run/zumastor/mount/$vol /zsoftware

# start the large volume copy via nfs
rsync -avz --exclude '.snapshot*' $copy_path /zsoftware/ &
;;

monitor)
# monitoring the replication status
while true; do
	monitor_replication $vol $source $target
	sleep 30
done
;;

stop)
mountpoint -q /zsoftware && sudo umount /zsoftware
ssh $SSH_OPTS $source "/etc/init.d/nfs-kernel-server stop"
ssh $SSH_OPTS $source "zumastor forget volume $vol"
ssh $SSH_OPTS $source "/etc/init.d/nfs-kernel-server start"
ssh $SSH_OPTS $target "zumastor forget volume $vol"
;;

esac

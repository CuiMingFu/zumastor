#!/bin/bash
#
# Copy a large volume to an exported zumastor volume via nfs, with hourly
# replication running between the zumastor-nfs server and a backup server.
#
# Copyright 2007 Google Inc. All rights reserved.
# Author: Jiaying Zhang (jiayingz@google.com)
set -e

# change the configurations according to the machine setup
################################################################
# Source/target machine fqdns
test -n "$SOURCE_MACHINE" || SOURCE_MACHINE="host1.debian.org"
test -n "$TARGET_MACHINE" || TARGET_MACHINE="host1.debian.org"
# Name to use with zumastor define volume
test -n "$VOLUME_NAME"    || VOLUME_NAME="software"
# Source/target machine orgin/snapshot device names
test -n "$SOURCE_ORGDEV"  || SOURCE_ORGDEV="/dev/sysvg/vol-350G"
test -n "$SOURCE_SNAPDEV" || SOURCE_SNAPDEV="/dev/sysvg/vol-100G"
test -n "$TARGET_ORGDEV"  || TARGET_ORGDEV="/dev/sysvg/vol-350G"
test -n "$TARGET_SNAPDEV" || TARGET_SNAPDEV="/dev/sysvg/vol-100G"
# Source to get data to use in the big copy test run
test -n "$COPY_SOURCE"    || COPY_SOURCE="/terabyte-volume"
# Email address to send status updates to
test -n "$NOTIFY_EMAIL"   || NOTIFY_EMAIL="zumastor@debian.org"
# Should the test install packages?
test -n "$INSTALL_PKGS"   || INSTALL_PKGS="true"
# set this to false if you want to check grub and reboot manually
test -n "$AUTO_REBOOT"    || AUTO_REBOOT="false"
# Version of zumastor to test
test -n "$ZUMASTOR_VER"   || ZUMASTOR_VER="0.10.0"
# HTTP Base for downloading packages
test -n "$HTTP_BASE"      || HTTP_BASE="http://zumabuild"
################################################################

SSH_OPTS="-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -q -l root"
SCP_OPTS="-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -q"


ssh_setup() {
	local host=$1
	if ! ssh $SSH_OPTS -o PasswordAuthentication=false $host "/bin/true"
	then
		echo "set up ssh access to host $host"
		scp $SCP_OPTS ~/.ssh/id_rsa.pub root@$host:/tmp/
		ssh $SSH_OPTS $host "cat /tmp/id_rsa.pub >> /root/.ssh/authorized_keys"
	fi
}

remote_ssh_setup() {
	local source=$1
	local target=$2
	if ! ssh $SSH_OPTS $source "ssh $SSH_OPTS -o PasswordAuthentication=false $target /bin/true"
	then
		ssh $SSH_OPTS $source "ls /root/.ssh/id_rsa.pub" || ssh $SSH_OPTS $source "ssh-keygen -t rsa -f /root/.ssh/id_rsa -P ''"
		scp $SCP_OPTS root@$source:/root/.ssh/id_rsa.pub ~/.ssh/$source.pub
		scp $SCP_OPTS ~/.ssh/$source.pub root@$target:/root/.ssh/
		ssh $SSH_OPTS $target "cat /root/.ssh/$source.pub >> /root/.ssh/authorized_keys"
	fi
}

zuma_install() {
	local version=$1
	local source=$2
	local target=$3
	local revision

	rm -rf zumabuild
	mkdir zumabuild
	cd zumabuild
	wget $HTTP_BASE/trunk/testrev
	read revision <testrev
	echo version $version revision $revision
	wget $HTTP_BASE/trunk/r$revision/ddsnap_${version}-r${revision}_i386.deb
	wget $HTTP_BASE/trunk/r$revision/zumastor_${version}-r${revision}_all.deb
	wget $HTTP_BASE/trunk/kernel-headers-build_i386.deb
	wget $HTTP_BASE/trunk/kernel-image-build_i386.deb
	cd ..
	ssh $SSH_OPTS $source "rm -rf /root/zumabuild"
	scp $SCP_OPTS -r zumabuild root@$source:/root/
	ssh $SSH_OPTS $source "dpkg -i --force-confnew /root/zumabuild/*.deb" || { echo "fail to install zumastor packages on host $source"; exit 1; }
	ssh $SSH_OPTS $target "rm -rf /root/zumabuild"
	scp $SCP_OPTS -r zumabuild root@$target:/root/
	ssh $SSH_OPTS $target "dpkg -i --force-confnew /root/zumabuild/*.deb" || { echo "fail to install zumastor packages on host $target"; exit 1; }
}

check_ready() {
	local host=$1
	local revision version
	ssh_ready="false"
	for i in `seq 10`; do
		if ssh $SSH_OPTS $host "/bin/true"
		then
			ssh_ready="true"
			break;
		fi
		sleep 20
	done
	[ $ssh_ready = "false" ] && return 1

	read revision < zumabuild/buildrev
	version=`ssh $SSH_OPTS $host "zumastor --version" | awk '{ print $4 }'`
	[ $version = $revision ] || { echo "fail to install zumastor-$revision package"; exit 1; }
	version=`ssh $SSH_OPTS $host "ddsnap --version" |  head -n 1 | cut -d "\"" -f 2`
	[ $version = $revision ] || { echo "fail to install ddsnap-$revision package"; exit 1; }
	revision=`dpkg -I zumabuild/kernel-image-build_i386.deb | awk '/Description:/ { print $8 }'`
	revision=${revision/%./}
	version=`ssh $SSH_OPTS $host "uname -r"`
	[ $version = $revision ] || { echo "fail to install kernel-image-$revision package"; exit 1; }

	return 0
}

monitor_replication() {
	local vol=$1
	local smodify tmodify scurrent tcurrent shangtime thangtime

	if ssh $SSH_OPTS $source "ls /var/lib/zumastor/volumes/$VOLUME_NAME/targets/$target/send >& /dev/null"; then
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/targets/$target/send"`
	elif ssh $SSH_OPTS $source "ls /var/lib/zumastor/volumes/$VOLUME_NAME/targets/$target/hold >& /dev/null"; then
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/targets/$target/hold"`
	else
		smodify=`ssh $SSH_OPTS $source "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/targets/$target"`
	fi

	if ssh $SSH_OPTS $target "ls /var/lib/zumastor/volumes/$VOLUME_NAME/source/apply >& /dev/null"; then
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/source/apply"`
	elif ssh $SSH_OPTS $target "ls /var/lib/zumastor/volumes/$VOLUME_NAME/source/hold >& /dev/null"; then
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/source/hold"`
	else
		tmodify=`ssh $SSH_OPTS $target "stat -c %Y /var/lib/zumastor/volumes/$VOLUME_NAME/source"`
	fi

	scurrent=`ssh $SSH_OPTS $source "date +%s"`
	tcurrent=`ssh $SSH_OPTS $target "date +%s"`
	shangtime=$(( $scurrent - $smodify ))
	thangtime=$(( $tcurrent - $tmodify ))
	if [ $shangtime -ge 14400 ] || [ $thangtime -ge 14400 ]
	then
		echo "replication has stopped for four hours"
		echo | mail -s 'ZUMASTOR LARGE VOLUME COPY TEST HANGING' $notifyemail
		exit 1
	fi

	df -h | grep zsoftware
	pgrep rsync >& /dev/null || { echo | mail -s 'ZUMASTOR LARGE VOLUME COPY TEST FINISHED' $notifyemail ; exit 0; }
}

[ $# -eq 1 ] || { echo "usage: $0 install|start|monitor|stop"; exit 1; }
case $1 in
install)
# initial ssh setup, requires user interaction
[ -e ~/.ssh/id_rsa.pub ] || ssh-keygen -t rsa -f ~/.ssh/id_rsa -P ''
ssh_setup $SOURCE_MACHINE
ssh_setup $TARGET_MACHINE
remote_ssh_setup $SOURCE_MACHINE $TARGET_MACHINE
remote_ssh_setup $TARGET_MACHINE $SOURCE_MACHINE

# install/upgrade zumastor packages
[ "x$INSTALL_PKGS" = "xtrue" ] && zuma_install $ZUMASTOR_VER $SOURCE_MACHINE $TARGET_MACHINE
if [ "x$AUTO_REBOOT" = "xtrue" ]
then
	ssh $SSH_OPTS $SOURCE_MACHINE "reboot"
	ssh $SSH_OPTS $TARGET_MACHINE "reboot"
	sleep 30
	check_ready $SOURCE_MACHINE || { echo "fail to reboot host $SOURCE_MACHINE"; exit 1; }
	check_ready $TARGET_MACHINE || { echo "fail to reboot host $TARGET_MACHINE"; exit 1; }
fi
;;

start)
# zumastor volume/replication setup and the initial replication cycle
ssh $SSH_OPTS $SOURCE_MACHINE "zumastor define volume -i $VOLUME_NAME $SOURCE_ORGDEV $SOURCE_SNAPDEV"
ssh $SSH_OPTS $SOURCE_MACHINE "mkfs.ext3 /dev/mapper/$VOLUME_NAME"
ssh $SSH_OPTS $SOURCE_MACHINE "zumastor define master $VOLUME_NAME"
ssh $SSH_OPTS $SOURCE_MACHINE "zumastor define schedule $VOLUME_NAME -h 24 -d 7"
ssh $SSH_OPTS $SOURCE_MACHINE "zumastor define target $VOLUME_NAME $TARGET_MACHINE"
ssh $SSH_OPTS $TARGET_MACHINE "zumastor define volume -i $VOLUME_NAME $TARGET_ORGDEV $TARGET_SNAPDEV"
ssh $SSH_OPTS $TARGET_MACHINE "zumastor define source $VOLUME_NAME $SOURCE_MACHINE -p 3600 -m"
ssh $SSH_OPTS $TARGET_MACHINE "zumastor define schedule $VOLUME_NAME -h 24 -d 7"
ssh $SSH_OPTS $TARGET_MACHINE "zumastor start source $VOLUME_NAME"

# set up nfs access
ssh $SSH_OPTS $SOURCE_MACHINE "exportfs -o'rw,fsid=1000,crossmnt,nohide' *:/var/run/zumastor/mount/$VOLUME_NAME"
ssh $SSH_OPTS $SOURCE_MACHINE "chmod a+rw /var/run/zumastor/mount/$VOLUME_NAME"
[ -e /zsoftware ] || { sudo mkdir /zsoftware; sudo chmod a+rwx /zsoftware; }
sudo mount -tnfs $SOURCE_MACHINE:/var/run/zumastor/mount/$VOLUME_NAME /zsoftware

# start the large volume copy via nfs
rsync -avz --exclude '.snapshot*' $COPY_SOURCE /zsoftware/ &
;;

monitor)
# monitoring the replication status
while true
do
	monitor_replication $VOLUME_NAME $SOURCE_MACHINE $TARGET_MACHINE
	sleep 30
done
;;

stop)
mountpoint -q /zsoftware && sudo umount /zsoftware
ssh $SSH_OPTS $SOURCE_MACHINE "/etc/init.d/nfs-kernel-server stop"
ssh $SSH_OPTS $SOURCE_MACHINE "zumastor forget volume $VOLUME_NAME"
ssh $SSH_OPTS $SOURCE_MACHINE "/etc/init.d/nfs-kernel-server start"
ssh $SSH_OPTS $TARGET_MACHINE "zumastor forget volume $VOLUME_NAME"
;;

*)
echo "usage: $0 install|start|monitor|stop"
exit 1

esac

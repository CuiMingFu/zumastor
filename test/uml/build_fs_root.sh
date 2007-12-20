#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# install all the required libraries and utilities based on the image downloaded from uml website

umount_exit() {
	echo $1
	umount /mnt
	exit 1
}

[[ $# -eq 1 ]] || { echo "Usage: build_fs.sh uml_fs"; exit 1; }
uml_fs=$1

mount -o loop $uml_fs /mnt || exit 1

echo -n Setting up apt-get...
cp /etc/resolv.conf /mnt/etc/resolv.conf \
	|| umount_exit 'Failed to copy resolv.conf to UML'
echo "deb ftp://ftp.us.debian.org/debian/ stable main contrib non-free" > /mnt/etc/apt/sources.list
chroot /mnt apt-get -q update
echo -e "done.\n"

echo -n Installing ssh...
chroot /mnt dpkg -s openssh-client >& /dev/null \
	|| chroot /mnt apt-get -y -q install openssh-client >& /dev/null \
	|| umount_exit 'Failed to install SSH in UML'
chroot /mnt dpkg -s openssh-server >& /dev/null \
	|| chroot /mnt apt-get -y -q install openssh-server >& /dev/null \
	|| umount_exit 'Failed to install openssh-server in UML'
chroot /mnt sh /etc/init.d/ssh stop
# to get rid of the need for initial confirmation
grep "StrictHostKeyChecking no" /mnt/etc/ssh/ssh_config >& /dev/null \
	|| echo "    StrictHostKeyChecking no" >> /mnt/etc/ssh/ssh_config
grep "VerifyHostKeyDNS yes" /mnt/etc/ssh/ssh_config >& /dev/null \
	|| echo "    VerifyHostKeyDNS yes" >> /mnt/etc/ssh/ssh_config
grep "UseDNS no" /mnt/etc/ssh/sshd_config >& /dev/null \
	|| echo "UseDNS no" >> /mnt/etc/ssh/sshd_config
echo -e "done.\n"

# Turn off unnecessary services
rm -f /mnt/etc/rc2.d/{S20exim4,S20openbsd-inetd}

# dmsetup, tree
echo -n Installing required utilities...
chroot /mnt dpkg -s dmsetup >& /dev/null \
	|| chroot /mnt apt-get -y -q install dmsetup >& /dev/null \
	|| umount_exit 'Failed to install dmsetup in UML'
chroot /mnt dpkg -s tree >& /dev/null \
	|| chroot /mnt apt-get -y -q install tree >& /dev/null \
	|| umount_exit 'Failed to install tree in UML'
echo -e "done.\n"

echo -n Upgrading ddsnap and zumastor...
DDSNAP_DPKG=`ls ddsnap_*.deb`
ZUMASTOR_DPKG=`ls zumastor_*.deb`
cp *.deb /mnt
chroot /mnt dpkg -i $DDSNAP_DPKG >& /dev/null \
	|| umount_exit 'Failed to install ddsnap in UML'
chroot /mnt dpkg -i $ZUMASTOR_DPKG >& /dev/null \
	|| umount_exit 'Failed to install zumastor in UML'
rm -f /mnt/*.deb
rm -f ./*.deb
echo -e "done.\n"

chroot /mnt apt-get clean >& /dev/null

echo -n Setting up ssh...
ls *.pub >& /dev/null && mkdir -p /mnt/root/.ssh \
	&& cat *.pub >> /mnt/root/.ssh/authorized_keys && rm *.pub
echo -e "done.\n"

echo -n Making nodes for ubdb and ubdc...
[[ -e /mnt/dev/ubdb ]] || mknod /mnt/dev/ubdb b 98 16
[[ -e /mnt/dev/ubdc ]] || mknod /mnt/dev/ubdc b 98 32
echo -e "done.\n"

umount /mnt

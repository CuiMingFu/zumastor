#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# install all the required libraries and utilities based on the image downloaded from uml website

[[ $# -eq 1 ]] || { echo "Usage: build_fs.sh uml_fs"; exit 1; }
uml_fs=$1

mount -o loop $uml_fs /mnt || exit 1

echo -n Setting up apt-get...
cp /etc/resolv.conf /mnt/etc/resolv.conf || { umount /mnt; exit 1; }
echo "deb ftp://ftp.us.debian.org/debian/ stable main contrib non-free" > /mnt/etc/apt/sources.list
chroot /mnt apt-get -q update
echo -e "done.\n"

echo -n Installing ssh...
chroot /mnt dpkg -s openssh-client >& /dev/null || chroot /mnt apt-get -y -q install openssh-client >& /dev/null || { umount /mnt; exit 1; }
chroot /mnt dpkg -s openssh-server >& /dev/null || chroot /mnt apt-get -y -q install openssh-server >& /dev/null || { umount /mnt; exit 1; }
chroot /mnt sh /etc/init.d/ssh stop
# to get rid of the need for initial confirmation
grep "StrictHostKeyChecking no" /mnt/etc/ssh/ssh_config >& /dev/null || echo "    StrictHostKeyChecking no" >> /mnt/etc/ssh/ssh_config
grep "VerifyHostKeyDNS yes" /mnt/etc/ssh/ssh_config >& /dev/null || echo "    VerifyHostKeyDNS yes" >> /mnt/etc/ssh/ssh_config
echo -e "done.\n"

# Turn off unnecessary services
rm -f /mnt/etc/rc2.d/{S20exim4,S20openbsd-inetd}

# dmsetup, tree
echo -n Installing required utilities...
chroot /mnt dpkg -s dmsetup >& /dev/null || chroot /mnt apt-get -y -q install dmsetup >& /dev/null || { umount /mnt; exit 1; }
chroot /mnt dpkg -s tree >& /dev/null || chroot /mnt apt-get -y -q install tree >& /dev/null || { umount /mnt; exit 1; }
echo -e "done.\n"

echo -n Upgrading ddsnap and zumastor...
DDSNAP_DPKG=`ls ddsnap_*.deb`
ZUMASTOR_DPKG=`ls zumastor_*.deb`
mv *.deb /mnt
chroot /mnt dpkg -i $DDSNAP_DPKG >& /dev/null || { umount /mnt; exit 1; }
chroot /mnt dpkg -i $ZUMASTOR_DPKG >& /dev/null || { umount /mnt; exit 1; }
rm -f /mnt/*.deb
echo -e "done.\n"

chroot /mnt apt-get clean >& /dev/null

echo -n Setting up ssh...
ls *.pub >& /dev/null && mkdir -p /mnt/root/.ssh && cat *.pub >> /mnt/root/.ssh/authorized_keys && rm *.pub
echo -e "done.\n"

echo -n Making nodes for ubdb and ubdc...
[[ -e /mnt/dev/ubdb ]] || mknod /mnt/dev/ubdb b 98 16
[[ -e /mnt/dev/ubdc ]] || mknod /mnt/dev/ubdc b 98 32
echo -e "done.\n"

umount /mnt

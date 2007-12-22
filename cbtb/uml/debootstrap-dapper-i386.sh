#!/bin/sh -x

#
# Make a tarball of the root system of a basic Ubunutu/dapper system,
# for use in preparing UML root partition images
#

# $Id$
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

pushd ../..
SRC=$PWD
BUILD_DIR="$SRC/build"
if [ -f SVNREV ] ; then
  SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV`
else
  SVNREV=`svn info zumastor | grep ^Revision:  | cut -d\  -f2`
fi
popd

ARCH=i386

# the testenv setup, see cbtb/host-setup/
VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

rootdir=`mktemp -d`
stagefile=`mktemp`

TESTDEPENDENCIES=openssh-server,cron,postfix,dmsetup
BUILDDEPEDENCIES=build-essential,lvm2,fakeroot,kernel-package,devscripts,subversion,debhelper,libpopt-dev,zlib1g-dev,debhelper,bzip2

# Basic dapper software installed into $rootdir
sudo debootstrap --arch $ARCH \
  --include=$TESTDEPENDENCIES,$BUILDDEPEDENCIES \
  dapper $rootdir http://$VIRTHOST/ubuntu

# Authorize the user to ssh into this virtual instance
cat ~/.ssh/*.pub >>$stagefile
sudo mkdir -p $rootdir/root/.ssh/
sudo mv $stagefile $rootdir/root/.ssh/authorized_keys
sudo chown root:root $rootdir/root/.ssh/authorized_keys
sudo chmod 600 $rootdir/root/.ssh/authorized_keys


echo unassigned >$stagefile
sudo mv $stagefile $rootdir/etc/hostname

sudo ssh-keygen -q -P '' -N '' -t dsa -f $rootdir/root/.ssh/id_dsa
sudo "cat $rootdir/root/.ssh/id_dsa.pub >> $rootdir/root/.ssh/authorized_keys"

cat >$stagefile <<EOF
# Used by ifup(8) and ifdown(8). See the interfaces(5) manpage or
# /usr/share/doc/ifupdown/examples for more information.

# The loopback network interface
auto lo
iface lo inet loopback

# The primary network interface
auto eth0
iface eth0 inet dhcp
EOF
sudo mv $stagefile $rootdir/etc/network/interfaces

# clean up the downloaded packages to conserve space
sudo rm -f $rootdir/var/cache/apt/archives/*.deb

# create and mount an empty ext3 filesystem
ext3dev=`mktemp`
ext3dir=`mktemp -d`
dd if=/dev/zero bs=1M seek=1024 count=0 of=$ext3dev
sudo mkfs.ext3 -F $ext3dev
sudo mount -oloop,rw $ext3dev $ext3dir

# tar it up the new filesystem onto the new ext3 device
sudo tar cf - -C $rootdir . | \
  sudo tar xf - -C $ext3dir

# create symlinks from sd* to the ubd* devices
sudo ln -s ubda $ext3dir/dev/sda
sudo ln -s ubdb $ext3dir/dev/sdb
sudo ln -s ubdc $ext3dir/dev/sdc
sudo ln -s ubdd $ext3dir/dev/sdd
cat >$stagefile <<EOF
KERNEL=="ubda",                 SYMLINK+="sda"
KERNEL=="ubdb",                 SYMLINK+="sdb"
KERNEL=="ubdc",                 SYMLINK+="sdc"
KERNEL=="ubdd",                 SYMLINK+="sdd"
EOF
sudo mv $stagefile $ext3dir/etc/udev/rules.d/61-ubd-symlinks.rules

sudo umount $ext3dir

# copy it back into the build/ directory, avoiding NFS issues and
# potential races.
sudo chown $USER $ext3dev
mv $ext3dev $BUILD_DIR/dapper-$ARCH-r$SVNREV.ext3
ln -sf dapper-$ARCH-r$SVNREV.ext3 $BUILD_DIR/dapper-$ARCH.ext3 

# cleanup
sudo rm -rf $rootdir
sudo rmdir $ext3dir

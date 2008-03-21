#!/bin/bash -x
# Build a 2.6.22 kernel package for Zumastor

VERSION="2.6.22-14.52"
ZUMAREVISION=`svn info ../../ | grep ^Revision:  | cut -d\  -f2`
PKGVERSION="$VERSION~zumappa$ZUMAREVISION"
MIRROR="http://archive.ubuntu.com/ubuntu"
DISTRIBUTION="gutsy"
CHANGELOG="Upstream Package with Zumastor.org r$ZUMAREVISION patches"

WORKDIR=`mktemp -d`

MIRRORDIR="$MIRROR/pool/main/l/linux-source-2.6.22"
for file in linux-source-2.6.22_2.6.22.orig.tar.gz \
            linux-source-2.6.22_2.6.22-14.52.dsc \
            linux-source-2.6.22_2.6.22-14.52.diff.gz
do
  wget $MIRRORDIR/$file -O $WORKDIR/$file
done

CWD=`pwd`
cd $WORKDIR
dpkg-source -x linux-source-2.6.22_2.6.22-14.52.dsc
cd linux-source-2.6.22
for patchfile in $CWD/patches-2.6.22/*.patch
do
  echo "Using Patch $patchfile"
  patch -p5 < $patchfile
done

mkdir debian/binary-custom.d/zumastor debian/binary-custom.d/zumastor/patchset

VERSIONSIG="CONFIG_VERSION_SIGNATURE=\"Ubuntu 2.6.24-4.6-zumastor\""

for arch in i386 amd64 lpia
do
  cp $CWD/../config/2.6.22.18-${arch}-full \
     debian/binary-custom.d/zumastor/config.${arch}
  echo $VERSIONSIG >> debian/binary-custom.d/zumastor/config.${arch}
done

patchnum=0
for patch in $CWD/../../ddsnap/patches/2.6.22.18/*
do
  cp $patch \
     debian/binary-custom.d/zumastor/patchset/00${patchnum}-`basename $patch`
  patchnum=$((${patchnum} + 1))
done

mkpatchdir=`mktemp -d`
autopatch=`mktemp`
mkdir -p $mkpatchdir/kernel-orig/drivers/md
mkdir -p $mkpatchdir/kernel-zumastor/drivers/md
cp $CWD/../../ddsnap/kernel/dm-ddsnap.* $mkpatchdir/kernel-zumastor/drivers/md
cd $mkpatchdir
diff -ruNp kernel-orig kernel-zumastor > $autopatch
cd $WORKDIR/linux-source-2.6.22
cp $autopatch \
   debian/binary-custom.d/zumastor/patchset/00${patchnum}-AUTOdm.patch
rm -rf $mkpatchdir
rm $autopatch

echo "# Placeholder" > debian/binary-custom.d/zumastor/rules

cp -r debian/abi/2.6.22-14.46 debian/abi/2.6.22-14.52

DEBEMAIL="Zumastor Builder <zuambuild@gmail.com>" dch -v $PKGVERSION \
         -D $DISTRIBUTION -b $CHANGELOG

dpkg-buildpackage -rfakeroot -S
echo "Results in $WORKDIR"

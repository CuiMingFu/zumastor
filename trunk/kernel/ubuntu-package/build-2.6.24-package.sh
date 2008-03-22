#!/bin/bash -x
# Build a 2.6.24 kernel package for Zumastor

VERSION="2.6.24-12.22"
ZUMAREVISION=`svn info ../../ | grep ^Revision:  | cut -d\  -f2`
PKGVERSION="$VERSION~zumappa$ZUMAREVISION"
MIRROR="http://mirrors.kernel.org/ubuntu"
DISTRIBUTION="hardy"
CHANGELOG="Upstream Package with Zumastor.org r$ZUMAREVISION patches"

WORKDIR=`mktemp -d`

wget $MIRROR/pool/main/l/linux/linux_2.6.24.orig.tar.gz -O \
  $WORKDIR/linux_2.6.24.orig.tar.gz
wget $MIRROR/pool/main/l/linux/linux_2.6.24-12.22.dsc -O \
  $WORKDIR/linux_2.6.24-12.22.dsc
wget $MIRROR/pool/main/l/linux/linux_2.6.24-12.22.diff.gz -O \
  $WORKDIR/linux_2.6.24-12.22.diff.gz

CWD=`pwd`
cd $WORKDIR
dpkg-source -x linux_2.6.24-12.22.dsc
cd linux-2.6.24
for patchfile in $CWD/patches-2.6.24/*.patch
do
  echo "Using Patch $patchfile"
  patch -p5 < $patchfile
done
mkdir debian/binary-custom.d/zumastor debian/binary-custom.d/zumastor/patchset


for arch in i386 amd64 lpia
do
  cat debian/config/${arch}/config debian/config/${arch}/config.generic \
   >> debian/binary-custom.d/zumastor/config.${arch}
  echo "CONFIG_DM_DDSNAP=m" >> debian/binary-custom.d/zumastor/config.${arch}
done

patchnum=0
for patch in $CWD/../../ddsnap/patches/2.6.24.2/*
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
cd $WORKDIR/linux-2.6.24
cp $autopatch \
   debian/binary-custom.d/zumastor/patchset/00${patchnum}-AUTOdm.patch
rm -rf $mkpatchdir
rm $autopatch

echo "# Placeholder" > debian/binary-custom.d/zumastor/rules

cp -r debian/abi/2.6.24-12.21 debian/abi/2.6.24-12.22

## What follows is a horrible hack. Until dch lets you force a distribution
## it does not know about, these will unfortunately remain.
DEBEMAIL="Zumastor Builder <zuambuild@gmail.com>" dch -v $PKGVERSION \
         -D warty-proposed -b $CHANGELOG
sed -i -e "s/warty-proposed/$DISTRIBUTION/" debian/changelog
## End Ugly Hack

dpkg-buildpackage -rfakeroot -S
echo "Results in $WORKDIR"

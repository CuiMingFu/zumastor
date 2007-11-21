#!/bin/sh
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# Download the base filesystem image from uml website

. config_uml

[[ $# -eq 1 ]] || { echo "Usage: build_fs.sh uml_fs"; exit 1; }
uml_fs=$1

fs_image=Debian-3.1-x86-root_fs
if [ -f $fs_image ]; then
  echo Using existing Debian uml root file system image.
else
  echo -n Getting Debian uml root file system image...
  wget -c -q http://uml.nagafix.co.uk/Debian-3.1/${fs_image}.bz2 || exit $?
  echo -n Unpacking root file system image...
  bunzip2 -k ${fs_image}.bz2 >> $LOG
  echo -e "done.\n"
fi

mv $fs_image $uml_fs
chmod a+rw $uml_fs

echo -n Setting up ssh keys for user $USER ...
mkdir -p ~/.ssh
[[ -e ~/.ssh/id_dsa.pub ]] || ssh-keygen -t dsa -f ~/.ssh/id_dsa -P '' >> $LOG
cp ~/.ssh/id_dsa.pub $USER.pub
chmod a+rw $USER.pub
echo -e "done.\n"

echo -n Building zumastor Debian package...
VERSION=`awk '/^[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' $ZUMA_REPOSITORY/Version`
if [ "x$VERSION" = "x" ] ; then
	echo "Suspect Version file"
	exit 1
fi

if [ -f $ZUMA_REPOSITORY/SVNREV ] ; then
	SVNREV=`awk '/^[0-9]+$/ { print $1; }' $ZUMA_REPOSITORY/SVNREV`
else
	pushd $ZUMA_REPOSITORY
	SVNREV=`svn info zumastor | grep ^Revision:  | cut -d\  -f2`
	popd
fi

pushd ${ZUMA_REPOSITORY}/zumastor >> $LOG || exit 1
[ -f debian/changelog ] && rm debian/changelog
VISUAL=/bin/true EDITOR=/bin/true dch --create --package zumastor -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG

pushd ${ZUMA_REPOSITORY}/ddsnap >> $LOG || exit 1
[ -f debian/changelog ] && rm debian/changelog
VISUAL=/bin/true EDITOR=/bin/true dch --create --package ddsnap -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || exit 1
dpkg-buildpackage -uc -us -rfakeroot >> $LOG || exit 1
popd >> $LOG
mv ${ZUMA_REPOSITORY}/ddsnap_$VERSION-r$SVNREV_*.deb .
mv ${ZUMA_REPOSITORY}/zumastor_$VERSION-r$SVNREV_*.deb .
rm ${ZUMA_REPOSITORY}/ddsnap_$VERSION-r$SVNREV_* ${ZUMA_REPOSITORY}/zumastor_$VERSION-r$SVNREV_*
chmod a+rw *.deb
echo -e "done.\n"

#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# Download the base filesystem image from uml website

. ./config_uml

[[ $# -eq 1 ]] || { echo "Usage: build_fs.sh uml_fs"; exit 1; }
uml_fs=$1

if [[ ! -e $uml_fs ]]; then
	echo -n Unpacking root file system image...
	bunzip2 -k $DOWNLOAD_CACHE/${fs_image}.bz2
	mv $DOWNLOAD_CACHE/${fs_image} .
	echo -e "done.\n"
	mv $fs_image $uml_fs
	chmod a+rw $uml_fs
fi

echo -n Setting up ssh keys for user $USER ...
mkdir -p ~/.ssh
[[ -e ~/.ssh/id_dsa.pub ]] || ssh-keygen -t dsa -f ~/.ssh/id_dsa -P ''
cp ~/.ssh/id_dsa.pub $USER.pub
chmod a+rw $USER.pub
echo -e "done.\n"

echo -n Building zumastor Debian package...
VERSION=`awk '/^[0-9]+\.[0-9]+(\.[0-9]+)?$/ { print $1; }' $ZUMA_REPOSITORY/Version`
if [ "x$VERSION" = "x" ] ; then
	echo "Suspect Version file"
	rm -f $uml_fs
	exit 1
fi

if [ -f $ZUMA_REPOSITORY/SVNREV ] ; then
	SVNREV=`awk '/^[0-9]+$/ { print $1; }' $ZUMA_REPOSITORY/SVNREV`
else
	pushd $ZUMA_REPOSITORY
	SVNREV=`svn info zumastor | grep ^Revision:  | cut -d\  -f2`
	popd
fi

pushd ${ZUMA_REPOSITORY}/zumastor || { rm -f $uml_fs; exit 1; }
[ -f debian/changelog ] && rm debian/changelog
VISUAL=/bin/true EDITOR=/bin/true dch --create --package zumastor -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || { rm -f $uml_fs; exit 1; }
dpkg-buildpackage -uc -us -rfakeroot || { rm -f $uml_fs; exit 1; }
popd

pushd ${ZUMA_REPOSITORY}/ddsnap || { rm -f $uml_fs; exit 1; }
[ -f debian/changelog ] && rm debian/changelog
VISUAL=/bin/true EDITOR=/bin/true dch --create --package ddsnap -u low --no-query -v $VERSION-r$SVNREV "revision $SVNREV" || { rm -f $uml_fs; exit 1; }
dpkg-buildpackage -uc -us -rfakeroot || { rm -f $uml_fs; exit 1; }
popd
mv ${ZUMA_REPOSITORY}/ddsnap_$VERSION-r$SVNREV_*.deb .
mv ${ZUMA_REPOSITORY}/zumastor_$VERSION-r$SVNREV_*.deb .
rm ${ZUMA_REPOSITORY}/ddsnap_$VERSION-r$SVNREV_* ${ZUMA_REPOSITORY}/zumastor_$VERSION-r$SVNREV_*
chmod a+rw *.deb
echo -e "done.\n"

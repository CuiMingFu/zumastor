#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# Unpack the base filesystem image downloaded from uml website, 
# setup ssh keys, and build zumastor debian packages

. ./config_uml

[[ $# -eq 1 ]] || { echo "Usage: build_fs.sh uml_fs"; exit 1; }
uml_fs=$1

rm_exit() {
	echo $1
	rm -f $uml_fs
	exit 1;
}

echo -n Packing ddsnap and zumastor source code...
pushd $ZUMA_REPOSITORY
tar czf ddsnap-src.tar.gz ddsnap
tar czf zumastor-src.tar.gz zumastor
popd
echo -e "done.\n"

pushd $WORKDIR

mv $ZUMA_REPOSITORY/ddsnap-src.tar.gz $ZUMA_REPOSITORY/zumastor-src.tar.gz .
chmod a+r ddsnap-src.tar.gz zumastor-src.tar.gz

cp $ZUMA_REPOSITORY/Version Version
if [ -f $ZUMA_REPOSITORY/SVNREV ] ; then
	cp $ZUMA_REPOSITORY/SVNREV .
else
	pushd $ZUMA_REPOSITORY
	svn info zumastor | grep ^Revision:  | cut -d\  -f2 > /tmp/SVNREV
	popd
	mv /tmp/SVNREV .
fi
chmod a+r Version SVNREV

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

popd


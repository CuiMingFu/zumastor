#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# build kernel linux uml with the KERNEL_VERSION specified in config_uml and with ddsnap kerenel patches

. config_uml

# Download a file and checks its checksum
download() {
	file=`basename "$1"`
	test -f $DOWNLOAD_CACHE/$file || (pushd $DOWNLOAD_CACHE; wget -c "$1"; popd)
	if [ "$2"x != ""x ]
	then
		echo "$2  $DOWNLOAD_CACHE/$file" > $DOWNLOAD_CACHE/$file.sha1sum
		sha1sum --status -c $DOWNLOAD_CACHE/$file.sha1sum
		if [[ $? -ne 0 ]]; then
			echo "$file sha1 checksum mismatch, try to re-download $file"
			pushd $DOWNLOAD_CACHE
			wget -c "$1"
			popd
			sha1sum --status -c $DOWNLOAD_CACHE/$file.sha1sum
		 	[[ $? -ne 0 ]] && { echo "$file sha1 checksum mismatch"; exit 1; }
		fi
	fi
}

[[ -e $DOWNLOAD_CACHE ]] || mkdir $DOWNLOAD_CACHE

echo -n Getting kernel sources from kernel.org ...
read sha1sum < $ZUMA_REPOSITORY/kernel/sha1sum/${KERNEL_VERSION}
download http://www.kernel.org/pub/linux/kernel/v2.6/linux-${KERNEL_VERSION}.tar.bz2 $sha1sum
echo -e "done.\n"

if [ -f $fs_image ]; then
	echo Using existing Debian uml root file system image.
else
	echo -n Getting Debian uml root file system image...
	download http://uml.nagafix.co.uk/Debian-3.1/${fs_image}.bz2 c9842a6e7fb0892b1377dfe6239e6c0ff2252f1e
	echo -e "done.\n"
fi


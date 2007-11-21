#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# build kernel linux uml with the KERNEL_VERSION specified in config_uml and with ddsnap kerenel patches

. config_uml

# Download a file and checks its checksum
download() {
	file=`basename "$1"`
	pushd $DOWNLOAD_CACHE
	wget -c "$1"
	popd
	if [ "$2"x != ""x ]
	then
		echo "$2  $DOWNLOAD_CACHE/$file" > $DOWNLOAD_CACHE/$file.sha1sum
		sha1sum --status -c $DOWNLOAD_CACHE/$file.sha1sum
		[[ $? -ne 0 ]] && { echo "$file sha1 checksum mismatch"; exit 1; }
	fi
}

[[ -e $DOWNLOAD_CACHE ]] || mkdir $DOWNLOAD_CACHE

echo -n Getting kernel sources from kernel.org ...
download http://www.kernel.org/pub/linux/kernel/v2.6/linux-${KERNEL_VERSION}.tar.bz2 22d5885f87f4b63455891e2042fcae96900af57a
echo -e "done.\n"

if [ -f $fs_image ]; then
	echo Using existing Debian uml root file system image.
else
	echo -n Getting Debian uml root file system image...
	download http://uml.nagafix.co.uk/Debian-3.1/${fs_image}.bz2 c9842a6e7fb0892b1377dfe6239e6c0ff2252f1e
	echo -e "done.\n"
fi


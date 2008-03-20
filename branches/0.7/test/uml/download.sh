#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# download linux kernel and debian root file system image

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
		 	if [[ $? -ne 0 ]]; then
				echo "$file sha1 checksum mismatch! "
		 		if [[ $SKIP_CHECKSUM_CHECKING == "yes" ]]; then
					echo "SKIP_CHECKSUM_CHECKING is set, so continue"
				else
					echo "Please verify that correct file is downloaded from $1"
					echo "You can overwrite the checksum checking option by setting SKIP_CHECKSUM_CHECKING=yes in config_uml"
					exit 1
				fi
			fi
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
	download http://uml.nagafix.co.uk/Debian-4.0/${fs_image}.bz2 10518efb28fa5840e41228f1d6c8a5f8e1e2b1d5
	echo -e "done.\n"
fi


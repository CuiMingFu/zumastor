#!/bin/bash
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
  bunzip2 ${fs_image}.bz2 >> $LOG
  echo -e "done.\n"
fi

mv $fs_image $uml_fs
chmod a+rw $uml_fs

if [[ $USER != "root" ]]; then
	mkdir -p ~/.ssh
	[[ -e ~/.ssh/id_dsa.pub ]] || ssh-keygen -t dsa -f ~/.ssh/id_dsa -P '' >> $LOG
	cp ~/.ssh/id_dsa.pub $USER.pub
	chmod a+rw $USER.pub
fi

[[ $USER == "root" ]] && ./build_fs_root.sh $uml_fs >> $LOG

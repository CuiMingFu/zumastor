#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# update zumastor and ddsnap

. config_uml

[[ $# -eq 1 ]] || { echo "Usage: update_zuma.sh uml_fs"; exit 1; }
uml_fs=$1

echo -n Upgrading ddsnap and zumastor...
mount -o loop $WORKDIR/$uml_fs /mnt || exit 1
pushd $ZUMA_REPOSITORY/ddsnap
make install prefix=/mnt
popd
pushd $ZUMA_REPOSITORY/zumastor
make install DESTDIR=/mnt
popd
umount /mnt || exit 1
echo -e "done.\n"


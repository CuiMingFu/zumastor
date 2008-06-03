#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# build kernel linux uml with the KERNEL_VERSION specified in config_uml and with ddsnap kerenel patches
. config_uml

pushd $WORKDIR
[[ -e linux-${KERNEL_VERSION} ]] && rm -rf linux-${KERNEL_VERSION}
echo -n Unpacking kernel...
tar xjfk $DOWNLOAD_CACHE/linux-${KERNEL_VERSION}.tar.bz2 || exit 1
echo -e "done.\n"

echo Generating patches...
pushd $ZUMA_REPOSITORY/ddsnap
make genpatches
popd

echo Applying patches...
cd linux-${KERNEL_VERSION} || exit 1
for patch in `ls $ZUMA_REPOSITORY/zumastor/patches/${KERNEL_VERSION}/* $ZUMA_REPOSITORY/ddsnap/patches/${KERNEL_VERSION}/* $ZUMA_REPOSITORY/test/uml/patches/${KERNEL_VERSION}/* 2>/dev/null`; do
        echo "   $patch"
        < $patch patch -p1 || exit 1
done
echo -e "done.\n"

echo -n Building kernel...
cp $ZUMA_REPOSITORY/kernel/config/${KERNEL_VERSION}-um-${ARCH}-uml .config
make oldconfig ARCH=um || exit 1
make linux ARCH=um || exit 1
cd ..
echo -e "done.\n"
popd

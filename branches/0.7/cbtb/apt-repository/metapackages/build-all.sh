#!/bin/bash
#
# Build all the metapackages
# ./build-all.sh kernel-image-name kernel-headers-name versionnumber


KERNEL_IMAGE=$1
KERNEL_HEADER=$2
REV=$3
sed -e "s/KERNEL_PACKAGE/$KERNEL_HEADER/" -e "s/VERSION/$REV/" kernel-headers-zumastor/DEBIAN/control.template > kernel-headers-zumastor/DEBIAN/control

sed -e "s/KERNEL_PACKAGE/$KERNEL_IMAGE/" -e "s/VERSION/$REV/" kernel-image-zumastor/DEBIAN/control.template > kernel-image-zumastor/DEBIAN/control

sed -e "s/VERSION/$REV/" zumastor-all/DEBIAN/control.template > zumastor-all/DEBIAN/control

dpkg-deb -b kernel-headers-zumastor kernel-headers-zumastor.deb
dpkg-deb -b kernel-image-zumastor kernel-image-zumastor.deb
dpkg-deb -b zumastor-all zumastor-all.deb
dpkg-name -s ../zumastor -o ./*.deb

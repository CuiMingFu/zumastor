#!/bin/bash
# zumastor automated build script
# Shapor Naghibzadeh <shapor@google.com>

KERNEL_VERSION=2.6.21.1
BUILD_DIR=build # relative to current directory
LOG=/dev/null
TIME=`date +%s`

[[ -e $1 ]] || { echo "Usage: $0 <path_to_kernel_config> [revision]"; exit 1; }

[[ $# -eq 2 ]] && REV="-r $2"

mkdir $BUILD_DIR 2>/dev/null
cp $1 $BUILD_DIR/$KERNEL_VERSION.config
pushd $BUILD_DIR >> $LOG

echo -ne Getting zumastor sources from subversion ...
if [[ -e zumastor ]]; then
	svn update $REV zumastor >> $LOG || exit $?
else
	svn checkout $REV http://zumastor.googlecode.com/svn/trunk/ zumastor >> $LOG || exit $?
fi
SVNREV=`svn info zumastor | grep ^Revision:  | cut -d\  -f2`
VERSION=`cat zumastor/Version` || exit 1
echo -e "done.\n"

echo -n Getting kernel sources from kernel.org ...
# use -c to prevent re-downloading the same kernel tarball more than once
wget -c http://www.kernel.org/pub/linux/kernel/v2.6/linux-${KERNEL_VERSION}.tar.bz2 >> $LOG || exit $?
echo -e "done.\n"


echo Starting package builds ...
pushd zumastor
echo $KERNEL_VERSION >KernelVersion
./buildcurrent.sh ../$KERNEL_VERSION.config
popd

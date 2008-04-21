#!/bin/sh

set -u # let make sure we use initialized variable

#setting up system parameters for nsnaps script

#Example:
#VOLUME_NAME= volume you are replicating
#ORIGIN_MOUNT= location of the volume's mount point
#TAR_DEV= tar device
#TAR_MNT= location of where TAR_DEV is mounted
#SOURCE_TAR= kernel source tree
#KERNEL_TAR= this is the
#ORIG_DEV= origin device volume
#SNAP_DEV= snap device volume
#META_DEV= meta device
#RAW_DEV= device to do raw testing
#SCRIPT_HOME= the directory path that hold the scripts


VOLUME_NAME=vol
ORIGIN_MOUNT=/vol
TAR_DEV=/dev/sdf1
TAR_MNT=/blah
SOURCE_TAR=${PWD}
KERNEL_TAR=linux-2.6.19.1.tar
ORIG_DEV=/dev/sdb2
SNAP_DEV=/dev/sdc2
META_DEV=/dev/umema
RAW_DEV=/dev/sysvg1/datasrc
SCRIPT_HOME=${PWD}
TEST_ROOT_DIR=${PWD}/output

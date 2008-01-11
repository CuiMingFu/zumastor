#!/bin/tcsh -f

set OBJ = "obj-`uname -s`-`uname -m`"
$FSTRESS_HOME/$OBJ/fstress_fill $*

exit $status
#EOF
#! /bin/tcsh -f

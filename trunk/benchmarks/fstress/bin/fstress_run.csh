#!/usr/local/bin/tcsh -f

set OBJ = "obj-`uname -s`-`uname -m`"
$FSTRESS_HOME/$OBJ/fstress_run $*

exit $status
#EOF

#!/bin/bash
# Copyright 2007 Google Inc.
# Author: Jiaying Zhang <jiayingz@google.com>

# run all tests in the current directory
export ITERATIONS=10  # each test runs ten iterations
for filename in $( find $1 -type f -name 'test_*.sh' | sort )
do
	test=`basename $filename .sh`
	logfile=log_$test
	sh $filename >& log_$test
	result=`tail -n 1 $logfile`
	echo test $test : $result
done

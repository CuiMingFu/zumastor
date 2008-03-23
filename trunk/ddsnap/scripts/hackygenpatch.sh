#!/bin/bash
basedir=`mktemp -d`
filepath="linux/drivers/md"
for dir in a b
do
  mkdir -p $basedir/$dir/$filepath
done

cp kernel/* $basedir/b/$filepath
diff -ruNp $basedir/a $basedir/b
rm -rf $basedir

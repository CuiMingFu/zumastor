#!/bin/bash
basedir=`mktemp -d`
filepath="linux/drivers/md"
for dir in a b
do
  mkdir -p $basedir/$dir-$filepath
done

cp kernel/* $basedir/b-$filepath
curdir=`pwd`
cd $basedir
diff -ruNp a-$filepath b-$filepath
cd $curdir
rm -rf $basedir

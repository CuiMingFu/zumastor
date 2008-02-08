#!/bin/bash
#
# Make/Update a makeshift apt repository from the build server


newrev=`curl -sq http://zumabuild/trunk/buildrev`
newver=`curl -sq http://zumastor.googlecode.com/svn/trunk/Version`
buildhost="http://zumabuild"
WGET="wget -qc"

function packagename {
  dpkg-deb --info $1|grep Package|cut -d' ' -f 3|head -n 1
}

echo "Getting $newrev"
echo "Version $newver"
# Make sure packages dir exists
if [ ! -d ./zumastor ]
then
  mkdir zumastor
fi

curdir=`pwd`
cd zumastor
$WGET "${buildhost}/trunk/r${newrev}/ddsnap_${newver}-r${newrev}_i386.deb"
$WGET "${buildhost}/trunk/r${newrev}/zumastor_${newver}-r${newrev}_i386.deb"
$WGET "${buildhost}/trunk/kernel-image-build_i386.deb"
$WGET "${buildhost}/trunk/kernel-headers-build_i386.deb"
kernel_image=`packagename kernel-image-build_i386.deb`
kernel_headers=`packagename kernel-headers-build_i386.deb`
cd ../metapackages
./build-all.sh $kernel_image $kernel_headers $newrev
cd ../zumastor
dpkg-name -o *
cd $curdir
dpkg-scanpackages -m ./zumastor ./override > Packages
gzip -qc Packages > Packages.gz

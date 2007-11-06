#!/bin/sh

#set -x

#
# Potentially modifiable parameters.
#
# Number of virtual images to create.
NUMIMAGES=1
# The number to start with.
STARTNUM=1
# Working directory, where images and ancillary files live.
WORKDIR=~/local
# The CD image to install.
CDIMAGE=~/cd/test.iso
# Suffix of disk image names.
IMGSUFFIX=.img
# Name of the main installed Qemu disk image.
DISKIMG=${WORKDIR}/qimage${IMGSUFFIX}
# Prefix of the name of the live image.  The image number is appended to this
# along with the suffix ".img", e.g. "qlive-1.img".
QLIVEPREFIX=qlive-
# IP network /24 prefix for virtual machine addresses.
IPNET=192.168.22
IPSTART=20
# Fake "ethernet" MAC address prefix.
MAC=00:e0:10:00:00
MACSTART=0
# Prefix of the volume to be used for Zumastor testing.
ZUMVOLUME=${WORKDIR}/zumavol-
# Name of the install directory.  This must match the name in the preseed file!
QINSTDIR=/zinst
# Where we put the "zstartup" link so it'll be run at boot.
QRUNDIR=/etc/rc2.d

#
# These are the list of packages upon which we may be dependent if we run
# Zumastor tests.
#
PACKAGEDEPS="tree dmsetup openssh-server make gcc libc6-dev"

canonicalize_hostname()
{
	oname=$1
	set `host ${oname} | sed -e 's/\.$//'`
	if [ "$3" = "not" -a "$4" = "found:" ]; then
		echo ""
		return 1
	fi
	case "$2" in
	has)	cname=$1;;
	domain)	cname=$5
		set `host ${cname} | sed -e 's/\.$//'`
		if [ "$3" = "not" -a "$4" = "found:" ]; then
			echo ${oname}
		fi
		;;
	*)	cname=$oname;;
	esac
	echo $cname
}


usage()
{
	echo "Usage: $0 [-i <image>] [-n <number of images> -s <start number>" >&2
	echo "Where <image> is the CD image from which to install the virtual images," >&2
	echo "<number of images> is the number of images to create, by default one and" >&2
	echo "<start number> is the number of the virtual image to begin with." >&2
	exit 1
}

while getopts "i:n:s:" option ; do
	case "$option" in
	i)	CDIMAGE=${OPTARG};;
	n)	NUMIMAGES=${OPTARG};;
	s)	STARTNUM=${OPTARG};;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
if [ $# -ge 1 ]; then
	usage
fi

echo "Building ${NUMIMAGES} qemu image(s)..."
#
# If the CD image path doesn't start with a "/" then it's relative to
# the current directory.
#
if [ "${CDIMAGE:0:1}" != "/" ]; then
	CDIMAGE=`pwd`/${CDIMAGE}
fi

cd ${WORKDIR}
#
# Create our own qemu-ifup.sh script.
#
cat <<EOF_qemu-ifup.sh >qemu-ifup.sh
#!/bin/sh

#
# If the qemu bridge doesn't exist, create it.
#
brctl show | grep qbr0 >/dev/null 2>&1
if [ \$? -ne 0 ]; then
	brctl addbr qbr0
	ifconfig qbr0 \$HOSTIP
fi
echo "Executing \$0"
echo "Bringing up \$1 \$QIP for bridged mode..."
sudo /sbin/ifconfig \$1 0.0.0.0 promisc up
echo "Adding \$1 to qbr0..."
sudo /usr/sbin/brctl addif qbr0 \$1
EOF_qemu-ifup.sh
chmod 755 qemu-ifup.sh
#
# Create the install script.
#
cat <<EOF_zinstall.sh >zinstall.sh
#!/bin/sh

cd /${QINSTDIR}
#
# Install our test startup script that will run when we reboot.
#
cp zstartup.sh /etc/init.d/zstartup
chmod 755 /etc/init.d/zstartup
ln -s ../init.d/zstartup ${QRUNDIR}/S99zstartup
#
# Install all potential dependencies.
#
/usr/bin/apt-get -y install ${PACKAGEDEPS}
#
# Set up ssh
#
mkdir -p /root/.ssh
cat hostkey.pub >>/root/.ssh/authorized_keys
#
# Set 'noapic' on the kernel(s) since qemu doesn't have one.
#
sed --in-place '/^kernel/s/$/ noapic/' /boot/grub/menu.lst >/dev/console 2>&1

#cat <<EOF_inittab >>/etc/inittab
#T0:23:respawn:/sbin/getty -L -l /bin/sh ttyS0 9600 vt100
#EOF_inittab

exit 0
EOF_zinstall.sh
chmod 755 zinstall.sh
#
# Create the test startup script.  This script runs at boot and performs any
# necessary post-installation setup, then removes itself.
#
cat <<EOF_zstartup >zstartup.sh
#!/bin/sh

#
# All we do here is send our configured IP address out our console.
#
#echo "**********************************" >>/dev/ttyS0
#echo "**********************************" >>/dev/ttyS0
ifconfig -a | grep eth | cut -f1 -d' ' >>/dev/ttyS0
sleep 1
ifconfig -a | grep eth | cut -f1 -d' ' >>/dev/ttyS0
sleep 1
ifconfig -a | grep eth | cut -f1 -d' ' >>/dev/ttyS0
sleep 1
ifconfig -a | grep eth | cut -f1 -d' ' >>/dev/ttyS0
#echo "**********************************" >>/dev/ttyS0
#echo "**********************************" >>/dev/ttyS0
#echo "**********************************" >>/dev/ttyS0

# Crank up a shell
/bin/sh -l </dev/ttyS0 >/dev/console 2>&1 &

# Remove the startup script now that it has fulfilled its purpose.
rm /etc/init.d/zstartup
rm ${QRUNDIR}/S99zstartup

# Shut the virtual system down so we can do the next one.
#shutdown -hP now

exit 0
EOF_zstartup
chmod 755 zstartup.sh
#
# Create the network-configuration script.  This script is run shortly
# after boot via the serial console connection; it receives the interface
# to configure, IP address and hostname of the virtual machine.
#
cat <<EOF_znet >znet
#/bin/sh

echo \$1 \$2 \$3
ifconfig \$1 inet \$2 netmask 255.255.255.0
hostname \$3
#
# Add extra interfaces to /etc/network/interfaces
#
cat <<EOF_network >>/etc/network/interfaces
auto \$1
iface \$1 inet static
	address \$2
	netmask 255.255.255.0
#auto eth2
#iface eth2 inet static
#	address 0.0.0.0
EOF_network

echo \$3 >/etc/hostname

ifdown eth1
ifup eth1

EOF_znet
chmod 755 znet

zinstar=${WORKDIR}/zinstar-$$.tar
#
# If we've already built the master image, don't do it again.
#
if [ ! -f ${DISKIMG} ]; then
	trap "rm -f ${zinstar} ${DISKIMG}; exit 1" 1 2 3 9
	echo "Building master..."
	#
	# Generate (if necessary) and grab our ssh key to hand to the virtual
	# machine.
	#
	if [ ! -f ~/.ssh/id_rsa.pub ]; then
		ssh-keygen -t rsa -f ~/.ssh/id_rsa -N "" -C `whoami`@`hostname`
	fi
	cp ~/.ssh/id_rsa.pub hostkey.pub
	#
	# Stuff scripts and keys into a tarfile.
	#
	tar cf ${zinstar} zinstall.sh zstartup.sh znet hostkey.pub
	#
	# Create the qemu disk image then boot qemu from our iso to do the
	# install.
	#
	qemu-img create -f qcow2 ${DISKIMG} 10G
	qemu -no-reboot -hdb ${zinstar} -cdrom ${CDIMAGE} -boot d ${DISKIMG}

	rm ${zinstar}
	trap - 1 2 3 9
fi


# Export the IP address variable.
export QIP QMAC QIF
export HOSTIP=${IPNET}.${IPSTART}
#
# Now clone the base image, boot the new image and collect its IP address.
#
imagenum=${STARTNUM}
NUMIMAGES=$(($NUMIMAGES + $STARTNUM - 1))
IPLIST=
while [ ${imagenum} -le ${NUMIMAGES} ]; do
	echo "Cloning image ${imagenum}..."
	# Clone the test disk image for our live instance.
	thisimg=${WORKDIR}/${QLIVEPREFIX}${imagenum}${IMGSUFFIX}
	thisname=${QLIVEPREFIX}${imagenum}
	qconsole=qemu-${imagenum}.cons
	cp ${DISKIMG} ${thisimg} &
	rm -f qemu-${imagenum}.pid qemu-${imagenum}.cons
	mkfifo qemu-${imagenum}.cons
	# Create the Zumastor test volume.
	zvol=${ZUMVOLUME}${imagenum}${IMGSUFFIX}
	qemu-img create -f qcow2 ${zvol} 32G
	# Compute new IP/MAC addresses, interface number.
	QIP=${IPNET}.$((${IPSTART} + ${imagenum}))
	QMAC=${MAC}:`printf "%2.2x" $((${MACSTART} + ${imagenum}))`
	QIF=eth${imagenum}
	# Wait for the test disk image copy to complete.
	wait
	#
	# Run qemu on the cloned image.  We configure the network and hostname
	# then leave the instance running.
	#
	echo '********** WARNING:  Running sudo.  You may have to type your password!'
	sudo qemu --pidfile qemu-${imagenum}.pid -serial pipe:${qconsole} -m 512 -hdb ${zvol} \
		-cdrom ${CDIMAGE} -boot c \
		-net nic,macaddr=${QMAC} -net tap,ifname=${QIF},script=${WORKDIR}/qemu-ifup.sh \
		${thisimg} &
	IFACE=
	while [ -z "${IFACE}" -o "${IFACE:0:3}" != "eth" ]; do
		IFACE=`dd if=${qconsole} ibs=1 count=10 | grep eth | head -1 | sed -e 's/\r//'`
	done
	#echo "New interface ${IFACE}"
	sleep 5
	#
	# Try to configure the interface.
	#
	echo -ne "\r\r" >>${qconsole}
	sleep 1
	echo -ne "\r\r" >>${qconsole}
	sleep 1
	echo -n "${QINSTDIR}/znet " >>${qconsole}
	sleep 1
	echo -n "${IFACE} ${QIP} ${thisname}" >>${qconsole}
	sleep 1
	echo -ne "\r" >>${qconsole}
	#
	# Disown the currently-running qemu; he's now on his own.
	#
	disown -a
	grep -v ${QIP} ~/.ssh/known_hosts >/tmp/kn.$$
	mv /tmp/kn.$$ ~/.ssh/known_hosts
	sleep 3
	ssh-keyscan ${QIP}
	hostname=`canonicalize_hostname ${QIP}`
	if [ "${hostname}" = "" ]; then
		hostname=${thisname}
	fi
	echo ${hostname} | ssh -o StrictHostKeyChecking=no root@${QIP} "cat >/etc/hostname"
	echo ${hostname} | ssh -o StrictHostKeyChecking=no root@${QIP} "cat >/etc/hostname"
	ssh -o StrictHostKeyChecking=no root@${QIP} "hostname ${hostname}"
	echo "${QIP}	${hostname}" | ssh -o StrictHostKeyChecking=no root@${QIP} "cat >>/etc/hosts"
	# Now reboot the virtual image and leave it running.
	ssh -o StrictHostKeyChecking=no root@${QIP} "/sbin/shutdown -r now"
	if [ "${IPLIST}" = "" ]; then
		IPLIST=${QIP}
	else
		IPLIST="${IPLIST}, ${QIP}";
	fi
	imagenum=`expr ${imagenum} + 1`
done

echo "Created ${NUMIMAGES} images with IP addresses ${IPLIST}" >&2

exit 0

#ISO-START

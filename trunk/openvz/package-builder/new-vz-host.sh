#!/bin/sh -x
# Setup an ubuntu instance under openvz
# Gets the ubuntu cache file, creates a network between host and guest, and
#  gives the guest 512M memory privs
# Author: Will Nowak <wan@ccs.neu.edu

ARCH=amd64
VMID=102
HOST_NAME=`hostname|cut -d'.' -f 1`
IPBASE="192.168.1"
PBSVN="http://zumastor.googlecode.com/svn/trunk/openvz/package-builder"

# OpenVZ Template Cache
VZTHOST="http://download.openvz.org"
VZTURL="${VZTHOST}/template/precreated/ubuntu-7.10-${ARCH}-minimal.tar.gz"
VZTPATH=/var/lib/vz/template/cache/ubuntu-7.10-${ARCH}-minimal.tar.gz
VZTNAME=`basename $VZTPATH .tar.gz`

# Apt Config
OUTSIDEUBUNTU="http://archive.ubuntu.com/ubuntu/"
OUTSIDEDEBIAN="http://ftp.us.debian.org/debian/"
OUTSIDEDEBIANSEC="http://mirror.steadfast.net/debian-security/"
GUESTDEBLINE="deb http://${IPBASE}.1/ubuntu gutsy main universe multiverse"
GUESTDEBLINE="${GUESTDEBLINE} restricted"

if [ ! -f ${VZTPATH} ]
then
  sudo wget ${VZTEMPLATEURL} -O ${VZTPATH}
fi

if [ -d /var/lib/vz/private/$VMID/ ]
then
  sudo vzctl stop $VMID
  sudo vzctl destroy $VMID
fi

# I picked the last OUI from http://standards.ieee.org/regauth/oui/oui.txt
# It was listed as 'PRIVATE' so probably little chance of collision. Still bad
# juju though.
randmac() {
  dd if=/dev/urandom bs=1 count=3 2>/dev/null | od -tx1 | head -1 \
  | cut -d' ' -f2- | awk '{ print "AC:DE:48:"$1":"$2":"$3 }' | tr a-z A-Z
}

VIRT_HOST_ETH=`randmac`
VIRT_VM_ETH=`randmac`

sudo vzctl create $VMID --ostemplate ${VZTNAME} \
                        --hostname "${HOST_NAME}-vz${VMID}"

# 512M ram
sudo vzctl set $VMID --meminfo privvmpages:8 --save
# No CPU limit
sudo vzctl set $VMID --cpulimit 0 --save
# 5G Disk Quota
sudo vzctl set $VMID --diskspace 5G:6G --save
sudo vzctl set $VMID --netif_add eth0,$VIRT_VM_ETH,vm${VMID},$VIRT_HOST_ETH \
                   --save

sudo vzctl start $VMID
sudo ifconfig vm${VMID} ${IPBASE}.1 netmask 255.255.255.248 \
                                    broadcast ${IPBASE}.7
sudo vzctl exec $VMID ifconfig eth0 ${IPBASE}.2 netmask 255.255.255.248 \
                                                broadcast ${IPBASE}.7
#
# Host proxy and such
#
sudo apt-get -y install apache2
sudo a2enmod proxy_http
sudo a2enmod mem_cache
sudo a2enmod cache
TMPCFG=`mktemp`
cat > ${TMPCFG} <<EOF
NameVirtualHost 192.168.1.1
<VirtualHost 192.168.1.1>
ServerName mirror.localnet
ProxyRequests Off
ProxyPreserveHost Off

<Proxy *>
  Order deny,allow
  Allow from 192.168.1.0/29
</Proxy>

<Directory /debian/>
  ProxyPass ${OUTSIDEDEBIAN}
  ProxyPassReverse ${OUTSIDEDEBIAN}
</Directory>
<Directory /debian-security/>
  ProxyPass  ${OUTSIDEDEBIANSEC}
  ProxyPassReverse ${OUTSIDEDEBIANSEC}
</Directory>

<Directory /ubuntu/>
  ProxyPass  ${OUTSIDEUBUNTU}
  ProxyPassReverse ${OUTSIDEUBUNTU}
</Directory>

LogLevel warn
CustomLog /var/log/apache2/proxy.log combined

</VirtualHost>
EOF

sudo cp ${TMPCFG} /etc/apache2/sites-available/proxy
sudo a2ensite proxy
sudo a2dissite 000-default
sudo /etc/init.d/apache2 reload
sudo vzctl exec $VMID "sh -c \"echo ${GUESTDEBLINE} > /etc/apt/sources.list\""
sudo vzctl exec $VMID apt-get update
sudo vzctl exec $VMID apt-get -y install cowdancer python-debian
sudo vzctl exec $VMID mkdir /build /build/incoming /build/complete \
                            /build/work /build/queued
sudo wget ${PBSVN}/builder.py -O /var/lib/vz/private/${VMID}/usr/bin/builder.py
sudo chmod 755 /var/lib/vz/private/${VMID}/usr/bin/builder.py

echo "Setup Complete!"
echo "To build packages, place source packages in:"
echo "  /var/lib/vz/private/${VMID}/build/incoming"
echo "Run 'sudo vzctl exec ${VMID} /usr/bin/builder.py'"
echo "Wait...."
echo "Retrieve complete packages from:"
echo "  /var/lib/vz/private/${VMID}/build/complete"

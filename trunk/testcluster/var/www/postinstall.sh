#!/bin/bash
#
# Copyright 2008 Google Inc. All Rights Reserved.
# Author: willn@google.com (Will Nowak)
touch /tmp/late

## SSH Key Nonsense
mkdir -p /root/.ssh
echo ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAQEAqj17Sn4GrTLxbMbteF1raimtkGcNM3loB+Y/VVPjtSdSh7vcDEqLhpJdtloVA8+isf7nzRyQAqSA74KYYI55hSIKK5ofDpDAoTcQFPivCYm82V2Gops8JZt8PY6SLtrHsv9yZCvqvSymMkuVScT6qi5dGCZYN1pvb+Vc8UqtEAef8WpyHc6hO5VAaJVWfcsLmyIIyCD/DorBTYwKC5BgLqgltZ5tKdF7LbbhGqN2fmlcrQfnYnNqVFVgOSZavgjPbayf/CsCetRaENzSKsmabTPpZoecco+f0TW5264BBKJ0ZgPBGz9NrFlUpDF6+qjks/eSU2H4DxrtKYpoqJF++Q== > /root/.ssh/authorized_keys
chmod 750 /root/.ssh

# Zero raid superblock on non-boot disks
# Create empty partition table on each
for nonboot in `echo $sorteddisks|tail -n2`
do
  echo "postinstall prep $nonboot"
  #mdadm --zero-superblock $nonboot || true
  #echo -e "o\nw\n"|fdisk /dev/$nonboot || true
done

wget -O /dev/null http://install.localnet/x/post-install-ping.py

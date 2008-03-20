#!/usr/bin/python2.4
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Setup a host machine with a gutsy openvz guest.

Prepare a host machine to run an openvz guest used for building packages.
Note: This probably needs more testing and documentation.
"""

__author__ = 'wan@ccs.neu.edu (Will Nowak)'

import subprocess
import logging
import os
import random
import socket
import sys

logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s %(levelname)s %(message)s')

class VzHostSetup:
  def __init__(self):
    self._veid     = 102
    self._vzctl    = '/usr/sbin/vzctl'
    self._ipbase   = '192.168.1'
    self._ovztemplateserver = 'http://download.openvz.org'
    self._templatecachedir = '/var/lib/vz/template/cache'
    self._ovzprivatedir = '/var/lib/vz/private'
    self._mirrors = {}
    self._debline = ('deb http://%s.1/ubuntu gutsy main universe multivese '
                     'restricted' % self._ipbase)
    self._instancepkgs = ['cowdancer', 'python-debian', 'build-essential']
    self._vzparams = {}
    self._arch     = self.getHostArch()
    self._hostname = socket.gethostname().split('.')[0]
    self._vzParams()
    self._setMirrors()
    self._ovztemplateurl = ('%s/template/precreated/'
      'ubuntu-7.10-%s-minimal.tar.gz' % (self._ovztemplateserver, self._arch))
    self._templatename = \
        self._ovztemplateurl.split('/')[-1].replace('.tar.gz','')
    self._templatepath = '%s/%s' % (self._templatecachedir,
                                    self._ovztemplateurl.split('/')[-1])



  def _setMirrors(self):
    logging.debug('Setting internal mirrors list')
    self._mirrors['ubuntu'] = 'http://archive.ubuntu.com/ubuntu/'
    self._mirrors['debian'] = 'http://ftp.us.debian.org/debian/'
    self._mirrors['debian-security'] = \
                  'http://mirrors.steadfast.net/debian-security/'

  def _vzParams(self):
    logging.debug('Setting internal params list')
    self._vzparams['meminfo'] = 'none'
    self._vzparams['cpulimit'] = '0'
    self._vzparams['numproc'] = '1000:2000'
    self._vzparams['numfile'] = '50000:60000'
    self._vzparams['numflock'] = '1000:2000'
    self._vzparams['dcachesize'] = '3840001:3850000'
    self._vzparams['kmemsize'] = '11355923:11377049'
    self._vzparams['diskspace'] = '20G:25G'

  def adjustQuota(self):
    ''' Set quotas with vzquota
    Turns out we need a good deal of inodes to build kernel packages.
    '''
    logging.info('Adjusting Quota')
    cmd = '/usr/sbin/vzquota setlimit %s -i 400000 -I 500000' % (self._veid)
    self._quietRun(cmd)

  def aquireVzTemplate(self):
    logging.info('Checking VZ Template')
    if os.access(self._templatepath, os.R_OK):
      logging.debug('VZ template %s exists and is readable' %
                   self._templatename)
    else:
      logging.debug('Need to aquire VZ template %s' % self._templatename)
      cmd = ('/usr/bin/wget %s -O %s' %
             (self._ovztemplateurl, self._templatepath))
      self._quietRun(cmd)

  def clearOldInstance(self):
    logging.info('Checking old instance status')
    path = '%s/%s/' % (self._ovzprivatedir, self._veid)
    if os.path.exists(path):
      logging.info('Clearing old VZ instance in %s' % path)
      self._quietRun('%s stop %s' % (self._vzctl, self._veid))
      self._quietRun('%s destroy %s' % (self._vzctl, self._veid))

  def virtualMac(self):
    oui_base = 'aa:de:48'
    def randhexchar(length=1):
      output_string = ''
      while len(output_string) < length:
        rint = random.randint(0,15)
        hexstr = hex(rint)
        (zero, hexchar) = hexstr.split('x')
        output_string += hexchar
      return output_string
    mac = '%s:%s:%s:%s' % (oui_base, randhexchar(2), randhexchar(2),
                           randhexchar(2))
    logging.debug('Generated virtual MAC %s' % mac)
    return mac

  def createVzInstance(self):
    logging.info('Creating VZ instance')
    cmd = ('%s create %s --ostemplate %s --hostname %s-vz%s' %
           (self._vzctl, self._veid, self._templatename, self._hostname,
            self._veid))
    self._quietRun(cmd)

  def apacheCfg(self):
    logging.info('Generating Apache Configuration')
    cfg  = 'NameVirtualHost %(ipbase)s.1\n'
    cfg += '<VirtualHost %(ipbase)s.1>\n'
    cfg += ' ServerName mirror.localnet\n'
    cfg += ' ProxyRequests Off\n'
    cfg += ' ProxyPreserveHost Off\n\n'
    cfg += ' <Proxy *>\n'
    cfg += '  Order deny,allow\n'
    cfg += '  Allow from %(ipbase)s.0/29\n'
    cfg += ' </Proxy>\n\n'
    for path in self._mirrors:
      cfg += ' <Directory /%s/>\n' % (path)
      cfg += '  ProxyPass %s\n' % (self._mirrors[path])
      cfg += '  ProxyPassReverse %s\n' % (self._mirrors[path])
      cfg += ' </Directory>\n\n'
    cfg += ' Loglevel warn\n\n'
    cfg += '</VirtualHost>'
    return cfg % {'ipbase':self._ipbase}

  def setupApache(self):
    logging.info('Setting up apache')
    cmds = ['/usr/bin/apt-get install apache2',
            '/usr/sbin/a2enmod proxy_http',
            '/usr/sbin/a2enmod mem_cache',
            '/usr/sbin/a2enmod cache']
    for cmd in cmds:
      self._quietRun(cmd)
    # Write Config
    logging.debug('Writing apache config')
    fh = open('/etc/apache2/sites-available/proxy', 'w')
    fh.write(self.apacheCfg())
    fh.close()
    cmds = ['/usr/sbin/a2ensite proxy',
            '/usr/sbin/a2dissite 000-default'
            '/etc/init.d/apache2 stop',
            '/etc/init.d/apache2 start']
    for cmd in cmds:
      self._quietRun(cmd)
    logging.info('Apache setup complete')

  def _quietRun(self, cmd):
    logging.debug('Quietly running %s' % cmd)
    sp = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    sp.wait()

  def createVzEther(self):
    logging.info('Creating VZ ethernet devices')
    if 'netif_add' not in self._vzparams:
      self._vzparams['netif_add'] = 'eth0,%s,vm%s,%s' % (self.virtualMac(),
                                                         self._veid,
                                                         self.virtualMac())

  def setVzParams(self):
    self.createVzEther()
    logging.info('Setting parameters')
    for parameter in self._vzparams:
      cmd = ('%s set %s --%s %s --save' %
             (self._vzctl, self._veid, parameter, self._vzparams[parameter]))
      self._quietRun(cmd)

  def getHostArch(self):
    logging.info('Determining Host Architecture')
    sp = subprocess.Popen('/bin/uname -m', stdout=subprocess.PIPE, shell=True,
                          stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    sp.wait()
    output = sp.stdout.read()
    machine_type = output.strip()
    if machine_type == 'x86_64':
      return 'amd64'
    else:
      return 'i386'

  def startAndConfigureInterfaces(self):
    logging.info('Configuring host and guest interfaces')
    cmds = ['%s start %s' % (self._vzctl, self._veid),
            ('/sbin/ifconfig vm%s %s.1 netmask 255.255.255.248 broadcast %s.7'
             % (self._veid, self._ipbase, self._ipbase)),
            ('%s exec %s /sbin/ifconfig eth0 %s.2 netmask 255.255.255.248 '
             'broadcast %s.7' % (self._vzctl, self._veid,
                                 self._ipbase, self._ipbase)),
           ]
    for cmd in cmds:
      self._quietRun(cmd)
    logging.info('Configuring host and guest interfaces complete')

  def setupInstance(self):
    logging.info('Starting in-instance setup')
    # Set apt source
    fh = open('%s/%s/etc/apt/sources.list' % (self._ovzprivatedir, self._veid),
              'w')
    fh.write(self._debline)
    fh.close()
    # update pkg lists
    self._quietRun('%s exec %s /usr/bin/apt-get -y update' % (self._vzctl,
                                                           self._veid))
    # Install packages
    self._quietRun('%s exec %s /usr/bin/apt-get -y install %s' %
                   (self._vzctl, self._veid, ' '.join(self._instancepkgs)))
    # Make build directories
    dirs = ['/build/incoming', '/build/complete', '/build/work',
            '/build/queued']
    self._quietRun('%s exec %s /bin/mkdir -p %s' % (self._vzctl, self._veid,
                                                    ' '.join(dirs)))
    logging.info('In-instance setup complete')

  def checkPreReqs(self):
    self._checkUID()
    self._checkHostPkgs()

  def _checkUID(self):
    if os.geteuid() != 0:
      logging.error('You must be root to use this script')
      sys.exit(1)

  def _checkHostPkgs(self):
    hostpkg = ['vzquota', 'vzctl', 'python']
    for pkg in hostpkg:
      cmd = '/usr/bin/dpkg-query -s %s' % pkg
      sp = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      if sp.wait() > 0:
        logging.error('Please install %s before running this script' % pkg)
        sys.exit(1)

  def setup(self):
    self.checkPreReqs()
    logging.info('Launching setup')
    self.aquireVzTemplate()
    self.clearOldInstance()
    self.createVzInstance()
    self.setVzParams()
    self.adjustQuota()
    self.startAndConfigureInterfaces()
    self.setupApache()
    self.setupInstance()
    logging.info('Setup Done')

if __name__ == '__main__':
  #TODO: Option parsing and stuff.
  v = VzHostSetup()
  v.setup()

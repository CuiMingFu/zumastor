#!/usr/bin/python

from optparse import OptionParser
from debian_bundle.deb822 import Dsc
import random
import os
import subprocess
import sys
import logging

logging.basicConfig(level=logging.DEBUG,
                        format='%(asctime)s %(levelname)s %(message)s',)

class KernelBuilder:
  def __init__(self, name, description_file,
               target_distribution, patch_directories, config_options,
               maintainer_email, version_string, label, i386=True,
               amd64=False, mirror='http://us.archive.ubuntu.com/ubuntu',
               base_config='generic'):
    self._description_file = description_file
    self._description_obj = None
    self._original_source_dir = None
    self._package_diff = None
    self._builddirectory = None
    self._initialpath = os.getcwd()
    self._distribution = target_distribution
    self._archlist = []
    self._mirror = mirror
    self._optslist = config_options # []
    self._srcpatchdirs = patch_directories #[]
    self._name = name
    self._appendversion = version_string
    self._basekernelconfig = base_config
    self._label = label
    self._email = maintainer_email
    if i386:
      self._archlist.append('i386')
    if amd64:
      self._archlist.append('amd64')

  def _quietRun(self, cmd):
    logging.debug('QRun: %s' % cmd)
    sp = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    sp.wait()

  def _sourceDir(self):
    if self._original_source_dir is None:
      description_file = '%s/%s' % (self._buildDirectory(),
                                    self._descriptionName())
      os.chdir(self._buildDirectory())
      logging.debug('Extracting source package')
      cmd = 'dpkg-source -x %s' % description_file
      sp = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
      sp.wait()
      for line in sp.stdout:
        if line[0:23] == 'dpkg-source: extracting':
          words = line.strip().split()
          self._original_source_dir = words[-1]
    return self._original_source_dir

  def _descriptionUrl(self):
    ''' Returns the URL of the dsc '''
    return '%s/%s' % (self._mirror, self._description_file)

  def _descriptionName(self):
    file = self._descriptionUrl().split('/')[-1]
    return file

  def _descriptionFolder(self):
    ''' Returns the URL folder the dsc is in '''
    file = self._descriptionName()
    folder_url = self._descriptionUrl().replace('/%s' % file, '')
    return folder_url

  def _descriptionObj(self):
    if self._description_obj is None:
      logging.debug('Downloading DSC and creating object')
      dir = self._buildDirectory()
      url = self._descriptionUrl()
      file = url.split('/')[-1]
      new_path = '%s/%s' % (dir, file)
      self._quietRun('wget -q %s -O %s' % (url, new_path))
      self._description_obj = Dsc(open(new_path))
    return self._description_obj

  def _version(self):
    return self._descriptionObj()['version']

  def _getFiles(self):
    logging.debug('Downloading files from Dsc file')
    for file in self._descriptionObj()['files']:
      url = '%s/%s' % (self._descriptionFolder(), file['name'])
      local_path = '%s/%s' % (self._buildDirectory(), file['name'])
      self._quietRun('wget %s -q -O %s' % (url, local_path))

  def _buildDirectory(self):
    if self._builddirectory is None:
      directory = None
      while directory is None:
        possible_directory = ('/tmp/kernelbuild.%s' %
                              (str(random.randrange(100,1000))))
        if not os.path.exists(possible_directory):
          directory = possible_directory
          os.mkdir(possible_directory)
          self._builddirectory = directory
          logging.debug('Creating build directory..... %s' % directory)
    return self._builddirectory

  def _addFlavorToBuild(self, flavorname):
    logging.info('Adding flavor to build rules')
    rulesddir = ('%s/%s/debian/rules.d' %
                 (self._buildDirectory(), self._sourceDir()))
    for arch in self._archlist:
      mkfile = '%s/%s.mk' % (rulesddir, arch)
      self._quietRun('sed -i -e "s/^custom_flavours.*/& %s/" %s' %
                     (flavorname, mkfile))
    commonvarsfile = '%s/0-common-vars.mk' % (rulesddir)
    self._quietRun('sed -i -e "s/^all_custom_flavours.*/& %s/" %s' %
                   (flavorname, commonvarsfile))

  def _addFlavorRules(self, name, customrulesfile=None):
    logging.info('Adding flavor rules file')
    # TODO: Do something with the custom file
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    fh = open('%s/rules' % flavordir,'w')
    fh.write('# Nothing to see here\n')
    fh.close()

  def _addFlavorVars(self, name, customvarsfile=None):
    logging.info('Adding flavor vars file')
    # TODO: Do something with the custom file
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    content_dict = {'arch':' '.join(self._archlist),
                    'supported':'Generic',
                    'target':'%s kernel' % name,
                    'desc':'%s edition' % name,
                    'bootloader':'grub',
                    'provides':'kvm-api-4, redhat-cluster-modules',
                    'section_image':'universe/base'}
    fh = open('%s/vars' % flavordir,'w')
    content = ''
    for key, value in content_dict.iteritems():
      content += '%s="%s"\n' % (key, value)
    fh.write(content)
    fh.close()

  def _addFlavorPatches(self, name):
    logging.info('Adding patches to flavor')
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    patchno = 0
    for srcpatchdir in self._srcpatchdirs:
      patchsetdir = '%s/patchset' % flavordir
      for filename in os.listdir(srcpatchdir):
        if filename[-6:] != '.patch':
          continue
        original_path = '%s/%s' % (srcpatchdir, filename)
        new_path = '%s/%03d-%s' % (patchsetdir, patchno, filename)
        self._quietRun('cp %s %s' % (original_path, new_path))
        patchno += 1
  
  def _abiFixup(self):
    logging.info('Fixing up abi bug')
    abidir = '%s/%s/debian/abi' % (self._buildDirectory(), self._sourceDir())
    current_abi_list = os.listdir(abidir)
    current_abi_list.sort()
    old_abi = current_abi_list[0]
    self._quietRun('cp -r %s/%s %s/%s' %
                   (abidir, old_abi, abidir, self._version()))

  def _addFlavorCustomConfigOpts(self, name, arch):
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    configfile = '%s/config.%s' % (flavordir, arch)
    for opt in self._optslist:
      self._quietRun('echo "%s" >> %s' % (opt, configfile))

  def _addFlavor(self, name, basekernelconfig='generic'):
    basekernelconfig = self._basekernelconfig
    logging.info('Adding kernel flavor')
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    if os.path.exists(flavordir):
      raise 'Did you already create flavor %s' % name
    os.mkdir(flavordir)
    os.mkdir('%s/patchset' % flavordir)
    for arch in self._archlist:
      os.system('cat %(base)s/%(source)s/debian/config/%(arch)s/config '
               '%(base)s/%(source)s/debian/config/%(arch)s/config.%(bflav)s > '
               '%(flavdir)s/config.%(arch)s' %
                {'base':self._buildDirectory(),
                 'source':self._sourceDir(), 'bflav':basekernelconfig,
                 'arch':arch, 'flavdir':flavordir})
      self._addFlavorCustomConfigOpts(name, arch)
    self._addFlavorToBuild(name)
    self._addFlavorRules(name)
    self._addFlavorVars(name)
    self._addFlavorPatches(name)

  def _updateChangeLog(self, maintainer, newversion, entry):
    logging.info('Updating changelog')
    changelogfile = ('%s/%s/debian/changelog' %
                     (self._buildDirectory(), self._sourceDir()))
    cmd = ('DEBEMAIL="%s" dch -v "%s" -D "%s" -c "%s" -b "%s"' %
           (maintainer, newversion, self._distribution, changelogfile, entry))
    sp = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE)
    sp.stdin.write('\n')
    sp.wait()

  def main(self):
    self._getFiles()
    self._addFlavor(self._name)
    self._abiFixup()
    self._updateChangeLog(self._email, '%s~%s' % (self._version(), self._appendversion),
                          self._label)

if __name__ == '__main__':
  parser = OptionParser()
  parser.add_option('-m', '--mirror',
                    default='http://us.archive.ubuntu.com/ubuntu',
                    help='Ubuntu Archive Mirror')
  parser.add_option('-r', '--release',
                    help='Release to build for (gutsy, hardy, etc)')
  parser.add_option('-d', '--dscfile',
                    help='path to dsc file eg. pool/main/l/linux/linux.dsc')
  parser.add_option('-p', '--patches', action='append', default=[],
                    help='Patch directory to insert into flavor')
  parser.add_option('-c', '--config', help='Append line to kernel config',
                    action='append', default=[])
  parser.add_option('-e', '--email',
                    default='Flavor Maker <flavormaker@unconfigured>',
                    help='Changelog style email \'User Bob <user@bob.com>\'',)
  parser.add_option('-v', '--version', default='flavormaker1',
                    help='Version to append with ~ to package name. eg: myppa1',)
  parser.add_option('-l', '--label', default='flavormaker modified package',
                    help='Label for this modified kernel. eg: Upstream with my'
                    ' patches',)
  parser.add_option('-i', '--i386', help='Build for i386',
                    action='store_true', default=False)
  parser.add_option('-a', '--amd64', help='Build for amd64',
                    action='store_true', default=False)
  parser.add_option('-n', '--name', help='Flavor Name')
  parser.add_option('-b', '--baseconfig',
                    help='Base Kernel Configuration (generic is default)',
                    default='generic')
  (options, args) = parser.parse_args()
  if options.release is None:
    print 'ERROR: You must provide a release to build for'
    parser.print_help()
    sys.exit(1)
  if options.dscfile is None:
    print 'ERROR: You must provide a kernel dsc path to build from'
    parser.print_help()
    sys.exit(1)
  if (not options.i386) and (not options.amd64):
    print options
    print 'ERROR: You must pick at least one arch to build for (-a or -i)'
    parser.print_help()
    sys.exit(1)

  flavor = KernelBuilder(options.name, options.dscfile, options.release,
                         options.patches, options.config, options.email,
                         options.version, options.label, options.i386,
                         options.amd64, options.mirror, options.baseconfig)
  flavor.main()

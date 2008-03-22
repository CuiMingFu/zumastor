#!/usr/bin/python

APTMIRROR="http://localhost:9999/ubuntu"

from debian_bundle.deb822 import Dsc
import random
import os
import subprocess
import logging
import logging

logging.basicConfig(level=logging.DEBUG,
                        format='%(asctime)s %(levelname)s %(message)s',)

class KernelBuilder:
  def __init__(self, description_file,
               target_distribution):
    self._description_file = description_file
    self._description_obj = None
    self._original_source_dir = None
    self._package_diff = None
    self._builddirectory = None
    self._initialpath = os.getcwd()
    self._distribution = target_distribution

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
    return '%s/%s' % (APTMIRROR, self._description_file)
  
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

  def _addFlavorToBuild(self, flavorname, archlist=None):
    logging.info('Adding flavor to build rules')
    if archlist is None:
      archlist = ['i386', 'amd64']
    rulesddir = ('%s/%s/debian/rules.d' %
                 (self._buildDirectory(), self._sourceDir()))
    for arch in archlist:
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
    content_dict = {'arch':'i386 amd64 lpia',
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

  def _addFlavorPatches(self, name, srcpatchdir):
    logging.info('Adding patches to flavor')
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    patchsetdir = '%s/patchset' % flavordir
    patchno = 0
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
    self._quietRun('cp -r %s/%s %s/%s' % (abidir, old_abi, abidir, self._version()))

  def _addFlavor(self, name, archlist=None, basekernelconfig='generic'):
    logging.info('Adding kernel flavor')
    if archlist is None:
      archlist = ['i386', 'amd64']
    flavordir = ('%s/%s/debian/binary-custom.d/%s' %
                 (self._buildDirectory(), self._sourceDir(), name))
    if os.path.exists(flavordir):
      raise 'Did you already create flavor %s' % name
    os.mkdir(flavordir)
    os.mkdir('%s/patchset' % flavordir)
    for arch in archlist:
      os.system('cat %(base)s/%(source)s/debian/config/%(arch)s/config '
               '%(base)s/%(source)s/debian/config/%(arch)s/config.%(bflav)s > '
               '%(flavdir)s/config.%(arch)s' %
                {'base':self._buildDirectory(),
                 'source':self._sourceDir(), 'bflav':basekernelconfig,
                 'arch':arch, 'flavdir':flavordir})
    self._addFlavorToBuild(name, archlist)
    self._addFlavorRules(name)
    self._addFlavorVars(name)
    self._addFlavorPatches(name, '/home/wan/zumastor/ddsnap/patches/2.6.24.2/')

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
    self._addFlavor('zumastor')
    self._abiFixup()
    self._updateChangeLog('Zumastor Builder <zuambuild@gmail.com>',
                          '%s~1' % (self._version()),
                          'Upstream Package with Zumastor.org patches')

if __name__ == '__main__':
  hardy_2624 = KernelBuilder('pool/main/l/linux/linux_2.6.24-12.22.dsc',
                             'hardya')
  hardy_2624.main()
  
  gutsy_2622 = KernelBuilder('pool/main/l/linux-source-2.6.22/linux-source-2.6.22_2.6.22-14.52.dsc', 'gutsy')
  gutsy_2622.main()

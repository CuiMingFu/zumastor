#!/usr/bin/python
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

''' Build a debian package from source packages with cowbuilder
'''

__author__ = 'Will Nowak <wan@ccs.neu.edu>'

import os
import md5
import time
import logging
import subprocess

from debian_bundle.deb822 import Dsc

LOG_FORMAT = '%(asctime)s %(levelname)s %(message)s'
logging.basicConfig(format=LOG_FORMAT, level=logging.DEBUG)

INCOMING_DIR='/build/incoming'
QUEUE_DIR='/build/queued'
BUILT_DIR='/build/complete'
WORK_DIR='/build/work'

class BuildQueue:
  def __init__(self):
    self._queue = []
    self._mirror = 'http://192.168.1.1/ubuntu'
    self._dist = 'gutsy'
    self._debline = '"deb %s %s universe multiverse"' % (self._mirror,
                                                        self._dist)
    self._cowcreated = False
    logging.info('Initialized Build Queue')
    logging.info('Build Distribution: %s' % self._dist)
    logging.info('Build Mirror: %s' % self._mirror)

  def initbuildenv(self):
    if not self._cowcreated:
      cmd = ('cowbuilder --create --mirror %s --othermirror %s '
             '--distribution %s' % (self._mirror, self._debline, self._dist))
      logging.info('Initializing Build Environment (%s)' % cmd)
      sp = subprocess.Popen(cmd, shell=True,# stdout=subprocess.PIPE,
                            stdin=subprocess.PIPE)#, stderr=subprocess.PIPE)
      sp.wait()
      self._cowcreated = True
      logging.info('Initializing Build Environment Complete')

  def updatebuildenv(self):
    cmd = ('cowbuilder --update --mirror %s --othermirror %s '
           '--distribution %s' % (self._mirror, self._debline, self._dist))
    logging.info('Update Build Environment (%s)' % cmd)
    sp = subprocess.Popen(cmd, shell=True, #stdout=subprocess.PIPE,
                          stdin=subprocess.PIPE)#, stderr=subprocess.PIPE)
    sp.wait()
    logging.info('Update Build Environment Complete')

  def builddsc(self, filename):
    cmd = 'cowbuilder --build %s --buildresult %s' % (filename, BUILT_DIR)
    logging.info('Building %s' % filename)
    sp = subprocess.Popen(cmd, shell=True, #stdout=subprocess.PIPE,
                          stdin=subprocess.PIPE) #, stderr=subprocess.PIPE)
    sp.wait()
    logging.info('Building %s complete' % filename)

  def add(self, item):
    logging.debug('Adding %s to Build Queue' % str(item))
    self._queue.append(item)

  def next(self, build=True):
    if len(self._queue):
      if build:
        return self._queue.pop(0)
      else:
        return self._queue[0]
    else:
      return False

  def size(self):
    return len(self._queue)

  def import_incoming(self):
    logging.debug('Trying to import incoming directory')
    incoming_contents = os.listdir(INCOMING_DIR)
    for file in incoming_contents:
      if file[-4:] != '.dsc':
        continue
      trial_item = QueueItem(file, incoming=True)
      valid = 'Failed'
      if trial_item.validate():
        valid = 'Success'
        trial_item.queue()
        self.add(trial_item)
      logging.debug('Trying %s ...... %s' % (file, valid))

  def build(self):
    self.initbuildenv()
    if self.size() > 0:
      self.updatebuildenv()
    while self.size() > 0:
      item = self.next()
      self.builddsc(item.dsc_file())
      item.finish()

class QueueItem:
  def __init__(self, dsc_file_path, incoming=False):
    self._incoming = incoming
    self._dsc_file_path = dsc_file_path
    self._md5 = None
    self._dscobj = None
    self.md5()

  def __str__(self):
    return '%s "%s"' % (self.dsc_file(), self.dscobject()['source'])

  def __eq__(self, other):
    return ((other.__class__ == QueueItem) and
            (other.md5() == self.md5()))

  def _md5_file(self, filename):
    m = md5.new()
    if os.access(filename, os.R_OK):
      fh = open(filename)
      while True:
        data = fh.read(8096)
        if not data:
          break
        m.update(data)
      return m.hexdigest()
    else:
      return None

  def dsc_file(self, override=None):
    if override is not None:
      var = override
    else:
      var = self._incoming
    if var:
      return '%s/%s' % (INCOMING_DIR, self._dsc_file_path)
    else:
      return '%s/%s' % (QUEUE_DIR, self._dsc_file_path)

  def md5(self):
    if self._md5 is None:
      self._md5 = self._md5_file(self.dsc_file())
    return self._md5

  def validate(self):
    logging.debug('Validating Dsc "%s"' % self.dsc_file())
    return (os.access(self.dsc_file(), os.R_OK) and
            self._validate_files())

  def dscobject(self):
    if self._dscobj is None:
      fh = open(self.dsc_file())
      self._dscobj = Dsc(fh)
    return self._dscobj

  def _validate_files(self):
    dsc = self.dscobject()
    for file in dsc['files']:
      if self._incoming:
        fpath = '%s/%s' % (INCOMING_DIR, file['name'])
      else:
        fpath = '%s/%s' % (QUEUE_DIR, file['name'])
      if self._md5_file(fpath) != file['md5sum']:
        return False
    return True

  def queue(self):
    if not self._incoming:
      raise 'Already in queue'
    dsc = self.dscobject()
    for file in dsc['files']:
      os.rename('%s/%s' % (INCOMING_DIR, file['name']),
                '%s/%s' % (QUEUE_DIR, file['name']))
    os.rename(self.dsc_file(True), self.dsc_file(False))
    self._incoming = False
    logging.debug('Queuing Dsc "%s"' % self.dsc_file())

  def finish(self):
    ''' remove dsc and others '''
    if self._incoming:
      raise 'Cannot finish non-queued job'
    dsc = self.dscobject()
    os.unlink(self.dsc_file(False))
    for file in dsc['files']:
      os.unlink('%s/%s' % (QUEUE_DIR, file['name']))
    logging.debug('Finishing Dsc "%s"' % self.dsc_file())


if __name__ == '__main__':
  q = BuildQueue()
  q.import_incoming()
  if q.size():
    q.build()

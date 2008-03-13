#!/usr/bin/python

__author__ = 'Will Nowak <wan@ccs.neu.edu>'

import os
import md5
import time
from debian_bundle.deb822 import Dsc

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

  def initbuildenv(self):
    if not self._cowcreated:
      os.system('cowbuilder --create --mirror %s --othermirror %s '
                '--distribution %s' % (self._mirror, self._debline, self._dist))
      self._cowcreated = True

  def add(self, item):
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
    incoming_contents = os.listdir(INCOMING_DIR)
    for file in incoming_contents:
      if file[-4:] != '.dsc':
        continue
      trial_item = QueueItem(file, incoming=True)
      if trial_item.validate():
        trial_item.queue()
        self.add(trial_item)

  def build(self):
    self.initbuildenv()
    if self.size() > 0:
      print 'Setting up build environment for this run'
      os.system('cowbuilder --update --mirror %s --othermirror %s '
                '--distribution %s' % (self._mirror, self._debline, self._dist))
    while self.size() > 0:
      item = self.next()
      print 'Building source package %s' % item.dscobject()['source']
      os.system('cowbuilder --build %s --buildresult %s' % (item.dsc_file(),
                                                            BUILT_DIR))
      item.finish()

class QueueItem:
  def __init__(self, dsc_file_path, incoming=False):
    self._incoming = incoming
    self._dsc_file_path = dsc_file_path
    self._md5 = None
    self._dscobj = None
    self.md5()

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

  def finish(self):
    ''' remove dsc and others '''
    if self._incoming:
      raise 'Cannot finish non-queued job'
    dsc = self.dscobject()
    os.unlink(self.dsc_file(False))
    for file in dsc['files']:
      os.unlink('%s/%s' % (QUEUE_DIR, file['name']))


if __name__ == '__main__':
  q = BuildQueue()
  print 'Trying to import'
  q.import_incoming()
  if q.size():
    print 'Trying builds'
    q.build()

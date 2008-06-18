import test
from autotest_utils import *
import os

# This test assumes zumastor/ddsnap are installed.
#

class zcbtb(test.test):
  version = 1

  def setupdevs(self, sizelist={}):
    print system('losetup -a')
    for i in range(10):
      system('losetup -d /dev/loop%d > /dev/null 2>&1 || true' % i)
    devnum = 0
    for size in sizelist:
      system('dd if=/dev/zero of=%s/dev%d bs=1M count=0 seek=%d' %
             (self.tmpdir, devnum, int(size)))
      system('losetup /dev/loop%d %s/dev%d' % (devnum, self.tmpdir,
                                               devnum))
      os.environ['DEV%dNAME' % (devnum + 1)] = '/dev/loop%d' % devnum
      devnum += 1

  def getdevsize(self, devnum):
    var = 'DEV%dSIZE=' % devnum
    varlength = len(var)
    for line in open(self._testpath):
      if line[0:varlength] == var:
        size = line[varlength:].strip()
        return size
    raise 'Could not find size for device %d' % devnum

  def getdevsizes(self):
    self._devsizelist = []
    for devnum in range(self._numdevs):
      devnum = devnum + 1
      size = self.getdevsize(devnum)
      self._devsizelist.append(size)

  def getnumdevs(self):
    for line in open(self._testpath):
      if line[0:8] == 'NUMDEVS=':
        return int(line[8:].strip())
    raise 'Could not get number of devices'

  def findtest(self, test):
     testpath = '%s/tests/%s' % (self.bindir, test)
     if os.path.exists(testpath):
       self._testname = test
       self._testpath = testpath
       self._devsizelist = []
     else:
       raise 'Could not locate test %s' % test

  def execute(self, test):
    # test is a string like '1/snapshot-zumastor-ext3.sh'
    self.findtest(test)
    self._numdevs = self.getnumdevs()
    self.getdevsizes()
    self.setupdevs(self._devsizelist)
    system(self._testpath)

  def cleanup(self):
    system('zumastor forget volume testvol || true')
    system('rm -f /dev/testdev1 /dev/testdev2 || true')
    system('losetup -d /dev/loop0 > /dev/null 2>&1 || true')
    system('losetup -d /dev/loop1 > /dev/null 2>&1 || true')

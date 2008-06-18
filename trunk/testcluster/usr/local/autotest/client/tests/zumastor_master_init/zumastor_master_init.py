import test
from autotest_utils import *
import os

class zumastor_master_init(test.test):
  version = 1

  def execute(self, slave):
    os.environ['TARGET'] = slave
    system('%s/master_init.sh' % self.bindir)


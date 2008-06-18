import test
from autotest_utils import *
import os

class zumastor_slave_init(test.test):
  version = 1

  def execute(self, source):
    os.environ['SOURCE'] = source
    system('%s/slave_init.sh' % self.bindir)


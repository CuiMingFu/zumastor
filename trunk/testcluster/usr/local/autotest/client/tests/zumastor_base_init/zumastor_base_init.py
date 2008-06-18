import test
from autotest_utils import *
import os

class zumastor_base_init(test.test):
  version = 1

  def execute(self):
    system('%s/all_init.sh' % self.bindir)


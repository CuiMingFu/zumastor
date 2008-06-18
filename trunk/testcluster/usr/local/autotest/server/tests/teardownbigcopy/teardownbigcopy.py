import test, time
import subprocess
import WebPowerSwitch

class teardownbigcopy(test.test):
        version = 1

        def setup(self):
          pass

        def execute(self, machines):
          self.machines = machines
          print 'Tearing down big copy test'

        def cleanup(self):
          time.sleep(10)
          self.reinstallmachines()

        def reinstallmachines(self):
          sps = []
          for machine in self.machines:
            print 'Starting reinstallation of %s' % machine
            cmd = '/usr/local/bin/autotestreinstall %s' % machine
            sp = subprocess.Popen(cmd, shell=True)
            sps.append((machine, sp))

          for item in sps:
            (machine, sp) = item
            sp.wait()
            if sp.returncode == 0:
              print 'Reinstall of %s success!' % machine
            else:
              print 'Reinstall of %s failed!' % machine

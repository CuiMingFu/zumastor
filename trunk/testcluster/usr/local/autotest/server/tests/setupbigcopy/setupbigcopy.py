import test, time
import subprocess
import WebPowerSwitch

class setupbigcopy(test.test):
        version = 1

        def setup(self):
          pass

        def execute(self, machines):
          self.machines = machines
          self.setupssh()
          self.installzumastor()
          time.sleep(10)

        def cleanup(self):
           pass

        def setupssh(self):
          for machine in self.machines:
            cmd = '/usr/local/bin/setuprootsshprivate %s' % machine
            sp = subprocess.Popen(cmd, shell=True)
            sp.wait()

        def installzumastor(self):
          sps = []
          for machine in self.machines:
            print 'Starting zumastor install on %s' % machine
            cmd = '/usr/local/bin/installzumastor %s' % machine
            sp = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE)
            sp.stdin.write('y\n')
            sps.append((machine, sp))
          for item in sps:
            (machine, sp) = item
            sp.wait()
            if sp.returncode == 0:
              print 'Zumastor install on %s success!' % machine
            else:
              print 'Zumastor install on %s failed!' % machine

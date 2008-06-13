#!/usr/bin/python
import os
def dump_config():
  fh = open('/var/www/boot.cfg')
  print 'Content-type: text/plain'
  print
  print fh.read()
  fh.close()
if os.environ.has_key('REMOTE_ADDR'):
  ip = os.environ['REMOTE_ADDR']
  if os.path.exists('/usr/local/zumatest/machine_status/installed/%s' % ip):
    pass
  else:
    dump_config()
else:
  dump_config()

#!/usr/bin/python
import os
print 'Content-type: text/html'
print
if os.environ.has_key('REMOTE_ADDR'):
  ip = os.environ['REMOTE_ADDR']
  try:
    fh = open('/usr/local/zumatest/machine_status/installed/%s' % ip, 'w')
    fh.write('installed.')
    fh.close()
    print 'ok'
  except:
    print 'oops'
else:
  print 'no ip?'

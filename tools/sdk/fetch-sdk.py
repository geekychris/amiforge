# Runs in the OS4 guest (Python 2): download the SDK archives from the host's HTTP server.
import urllib, os
base = 'http://192.168.100.2:8790/'
dest = 'DH0:SDK-Install/'
names = ['install-sdk'] + open(dest + 'files.txt').read().split()
log = open(dest + 'fetch.log', 'w')
for n in names:
    try:
        urllib.urlretrieve(base + n, dest + n)
        log.write('ok %s %d\n' % (n, os.path.getsize(dest + n)))
    except Exception, e:
        log.write('fail %s %s\n' % (n, e))
    log.flush()
log.write('DONE\n')
log.close()

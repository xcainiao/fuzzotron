ps aux | grep ./udpserver | grep -v grep | awk '{print $2}'

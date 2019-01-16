ps aux | grep ./bug | grep -v grep | awk '{print $2}'

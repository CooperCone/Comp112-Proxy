#!/usr/bin/env bash

PORT=8099
IP=127.0.0.1

./main $PORT &

sleep 0.1

# ./client $PORT &

curl -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt
curl http://h2020.myspecies.info/ > t2.txt
diff t1.txt t2.txt

rm -rf t1.txt
rm -rf t2.txt
#!/usr/bin/env bash

PORT=8064
IP=127.0.0.1

./main $PORT &
# valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose ./main $PORT &

sleep 0.1

# curl -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt
# curl http://h2020.myspecies.info/ > t2.txt
# diff t1.txt t2.txt

curl -x $IP:$PORT https://isetta.io/ > t1.txt
curl https://stackoverflow.com/ > t2.txt
diff t1.txt t2.txt

rm -rf t1.txt
rm -rf t2.txt
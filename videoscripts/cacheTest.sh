#!/usr/bin/env bash

PORT=9500
IP=127.0.0.1

TIMEFORMAT=%R

time (
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;

time curl -s -x $IP:$PORT http://bio.acousti.ca/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;

time curl -s -x $IP:$PORT http://aba.myspecies.info/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;

time curl -s -x $IP:$PORT http://neverssl.com/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;

time curl -s -x $IP:$PORT http://abcd.myspecies.info/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;

time curl -s -x $IP:$PORT http://abelhasbrasil.myspecies.info/ > t1.txt;
time curl -s -x $IP:$PORT http://h2020.myspecies.info/ > t1.txt;
echo -n "Total: "
)

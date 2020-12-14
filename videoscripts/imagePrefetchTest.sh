#!/usr/bin/env bash

PORT=9500
IP=127.0.0.1

TIMEFORMAT=%R

echo -n "Http GET time: " 
time wget -q http://h2020.myspecies.info/ -e http_proxy=$IP:$PORT > /dev/null

sleep 1.0

echo -n "Image 1 time: "
time wget -q http://h2020.myspecies.info/sites/h2020.myspecies.info/files/BIHorizons_logo_resized.png -e http_proxy=$IP:$PORT > /dev/null

echo -n "Image 2 time: "
time wget -q http://h2020.myspecies.info/misc/feed.png -e http_proxy=$IP:$PORT > /dev/null

echo -n "Image 3 time: "
time wget -q http://h2020.myspecies.info/sites/all/modules/custom/scratchpads/scratchpads_blocks/images/vbrant.png -e http_proxy=$IP:$PORT > /dev/null

echo -n "Image 4 time: "
time wget -q http://h2020.myspecies.info/sites/all/modules/custom/scratchpads/scratchpads_blocks/images/drupal_small.png -e http_proxy=$IP:$PORT > /dev/null

echo -n "Image 5 time: "
time wget -q http://h2020.myspecies.info/sites/all/modules/custom/scratchpads/scratchpads_blocks/images/scratchpads.png -e http_proxy=$IP:$PORT > /dev/null

# echo -n "Image 6 time: "
# time wget -q http://h2020.myspecies.info/sites/all/modules/custom/creative_commons/images/zero.png -e http_proxy=$IP:$PORT > /dev/null


rm -rf t1.txt

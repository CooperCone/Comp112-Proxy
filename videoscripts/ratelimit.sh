#!/bin/bash

echo "Without proxy:"
curl --output /dev/null https://www.nytimes.com

echo "With proxy:"
curl -x 192.168.1.188:9055 --output /dev/null https://www.nytimes.com

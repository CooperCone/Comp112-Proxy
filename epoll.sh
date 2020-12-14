#!/bin/bash

if curl -x 192.168.1.188:9055 https://www.youtube.com > /dev/null 2>/dev/null; then echo "Youtube fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.google.com > /dev/null 2>/dev/null; then echo "Google fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.airbnb.com > /dev/null 2>/dev/null; then echo "AirBnB fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.twitter.com > /dev/null 2>/dev/null; then echo "Twitter fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.facebook.com > /dev/null 2>/dev/null; then echo "Facebook fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.stackoverflow.com > /dev/null 2>/dev/null; then echo "StackOverflow fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.amazon.com > /dev/null 2>/dev/null; then echo "Amazon fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.github.com > /dev/null 2>/dev/null; then echo "Github fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.yahoo.com > /dev/null 2>/dev/null; then echo "Yahoo fetched" ; fi &
if curl -x 192.168.1.188:9055 https://www.netflix.com > /dev/null 2>/dev/null; then echo "Netflix fetched" ; fi &
wait

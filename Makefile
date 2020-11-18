files = src/*.c
headerDir = -Iinclude
libs = -lnsl

all: proxy client
	
proxy:
	gcc $(files) $(headerDir) $(libs) -o main

client:
	gcc test/client.c $(headerDir) -lnsl -o client

test: all
	./test.sh

clean:
	rm main
	rm client
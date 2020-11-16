files = src/*.c
headerDir = -Iinclude
libs = -lnsl

all: proxy client
	
proxy:
	gcc $(files) $(headerDir) $(libs) -o main

client:
	gcc test/client.c $(headerDir) -lnsl -o client

run: all
	./main

clean:
	rm main
	rm client
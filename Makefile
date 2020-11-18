files = src/*.c
headerDir = -Iinclude
libs = -lnsl
debugFlags = -ggdb3

all: proxy client

proxy:
	gcc -o main $(files) $(headerDir) $(libs) 

client:
	gcc -o client test/client.c $(headerDir) -lnsl 

test: all
	./test.sh

clean:
	rm main
	rm client
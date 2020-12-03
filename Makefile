files = src/*.c
headerDir = -Iinclude -Ilib/zlib/include
libs = -lnsl -lz
debugFlags = -g
# -ggdb3

all: proxy client

proxy:
	gcc $(debugFlags) -o main $(files) $(headerDir) $(libs)

client:
	gcc -o client test/client.c $(headerDir) -lnsl 

test: all
	./test.sh

clean:
	rm main
	rm client
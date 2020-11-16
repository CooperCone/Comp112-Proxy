files = src/*.c
headerDir = -Iinclude
libs = -lnsl

all:
	gcc $(files) $(headerDir) $(libs) -o main

run: all
	./main

clean:
	rm main
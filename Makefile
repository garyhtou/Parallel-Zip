CC=g++
CFLAGS=-Wall -Werror -pthread -O
all: pzip
pzip: pzip.cpp
	$(CC) $(CFLAGS) pzip.cpp -o pzip
rebuild: clean all
clean:
	rm -f pzip
CFLAGS = -std=c17 -Wall -Wextra -pedantic -g

all: run

run: build
	@./main

build: main.c
	@gcc $(CFLAGS) -o main main.c

clean:
	@rm -f main main.o

FLAGS = -std=c11 -Wall -Wextra -Werror -Wfatal-errors -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wformat=2 -Wnull-dereference -Wconversion -Wsign-conversion -D_POSIX_C_SOURCE=200809L
FLAGS_PRD = -O2 -march=native -fstack-protector-strong
FLAGS_DBG = -D DEBUG -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined

ifeq ($(shell uname -s),Linux)
    FLAGS_PRD += -fstack-clash-protection
	FLAGS_DBG += -fsanitize=leak
endif

LIB_ROOT = $(shell git rev-parse --show-toplevel 2>/dev/null)/lib
DIR = $(shell basename $(CURDIR))



build: lib.o

dbg: debug.o



lib.o: code.c
	echo "building pi object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) -c

debug.o: code.c
	echo "building pi debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) -c



clean c:
	$(MAKE) clean --directory=$(LIB_ROOT) -s

_clean:
	echo "cleaning pi $(DIR)"
	rm -f *.o
	$(MAKE) clean --directory=test



.PHONY: test
test t:
	$(MAKE) dbg --directory=$(LIB_ROOT) -s
	$(MAKE) _test -s

_test:
	$(MAKE) --directory=test

export

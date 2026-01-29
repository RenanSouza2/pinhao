FLAGS = -std=c11 -Wall -Wextra -Werror -Wfatal-errors -c -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wformat=2 -Wnull-dereference -Wconversion -Wsign-conversion -fstack-protector-strong
FLAGS_PRD = -O2 -march=native -mtune=native -flto -fomit-frame-pointer -fstack-clash-protection -pie -fPIE -Wl,-z,relro,-z,now,-z,noexecstack
FLAGS_DBG = -D DEBUG -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined,leak

LIB_ROOT = $(word 1,$(subst lib, ,$(CURDIR)))lib
DIR = lib$(word 2,$(subst lib, ,$(CURDIR)))



build: lib.o

dbg: debug.o



lib.o: code.c
	echo "building pi object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD)

debug.o: code.c
	echo "building pi debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG)



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

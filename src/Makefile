FLAGS = -std=gnu2x -Wall -Wextra -Werror -Wfatal-errors -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wformat=2 -Wnull-dereference -Wconversion -Wsign-conversion -fstack-protector-strong -D _POSIX_C_SOURCE=199309L
FLAGS_DBG = -D DEBUG -fsanitize=address

LIB = ../lib/lib.o
DGB = ../lib/debug.o
CLU = ../mods/clu/bin/bin.o

DIR = src



build: main.o

dbg: debug.o



main.o: main.c $(LIB)
	echo "building pi exec $(DIR)"
	gcc -o $@ $^ $(FLAGS)

debug.o: main.c $(DGB) $(CLU)
	echo "building pi debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG)



.PHONY: $(LIB)  $(CLU)
$(LIB) $(CLU):
	$(MAKE) --directory=$(dir $@)

.PHONY: $(DGB)
$(DGB):
	$(MAKE) dbg --directory=$(dir $@)



clean:
	echo cleaning "num $(DIR)"
	rm -rf *.o
	$(MAKE) clean --directory=$(dir $(LIB))

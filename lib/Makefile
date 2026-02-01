FLAGS = -std=c11 -Wall -Wextra -Werror -Wfatal-errors -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wformat=2 -Wnull-dereference -Wconversion -Wsign-conversion -D_POSIX_C_SOURCE=200809L
FLAGS_PRD = -O2 -march=native -fstack-protector-strong
FLAGS_DBG = -D DEBUG -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined

ifeq ($(shell uname -s),Linux)
    FLAGS_PRD += -fstack-clash-protection
	FLAGS_DBG += -fsanitize=leak
endif



DIRS := $(wildcard */.)

LIB_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))lib.o)
DGB_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))debug.o)
CLU_FILE = ../mods/clu/bin/clu.o
NUM_FILE = ../mods/number/lib/lib.o

DIR = $(shell basename $(CURDIR))



build: lib.o

dbg: debug.o



lib.o: $(LIB_FILES) $(NUM_FILE)
	echo " linking pi object $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_PRD) -o $@ $^ -flto -nostdlib

debug.o: $(DGB_FILES) $(NUM_FILE) $(CLU_FILE)
	echo " linking pi debug $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_DBG)  -o $@ $^ -flto -nostdlib



.PHONY: $(LIB_FILES) $(NUM_FILE) $(CLU_FILE)
$(LIB_FILES) $(NUM_FILE) $(CLU_FILE):
	$(MAKE) --directory=$(dir $@)

.PHONY: $(DGB_FILES)
$(DGB_FILES):
	$(MAKE) dbg --directory=$(dir $@)



clean c:
	$(MAKE) _clean -s

_clean:
	echo "cleaning pi $(DIR)"
	rm -rf *.o
	for DIR in $(DIRS) ; do \
		( $(MAKE) _clean --directory=$$DIR ) || exit $$? ; \
	done
	$(MAKE) clean --directory $(dir ${CLU_FILE}) 
	$(MAKE) clean --directory $(dir ${NUM_FILE}) 



.PHONY: test
test t:
	$(MAKE) dbg -s
	$(MAKE) _test -s

_test:
	for DIR in $(DIRS) ; do \
		( $(MAKE) _test --directory=$$DIR ) || exit $$? ; \
	done

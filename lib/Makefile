DIRS := $(wildcard */.)

CLU_FILE = ../mods/clu/bin/bin.o
NUM_FILE = ../mods/number/lib/lib.o
LIB_FILES = $(foreach DIR,$(DIRS) $(NUM_FILE),$(dir $(DIR))lib.o)
DGB_FILES = $(foreach DIR,$(DIRS) $(NUM_FILE),$(dir $(DIR))debug.o)

DIR = lib$(word 2,$(subst lib, ,$(CURDIR)))



build: lib.o

dbg: debug.o $(CLU_FILE)



lib.o: $(LIB_FILES)
	echo " linking num object $(DIR)"
	gcc -r -o $@ $^ -flto

debug.o: $(DGB_FILES)
	echo " linking num debug $(DIR)"
	gcc -r -o $@ $^ -fsanitize=address,undefined,leak



.PHONY: $(LIB_FILES) $(CLU_FILE)
$(LIB_FILES) $(CLU_FILE):
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
	$(MAKE) clean --directory=$(dir $(CLU_FILE))
	$(MAKE) clean --directory=$(dir $(NUM_FILE))



.PHONY: test
test t:
	$(MAKE) dbg -s
	$(MAKE) _test -s

_test:
	for DIR in $(DIRS) ; do \
		( $(MAKE) _test --directory=$$DIR ) || exit $$? ; \
	done

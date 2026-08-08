PRJ_DIR = $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



DIRS := $(wildcard */.)

LIB_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))lib.o)
DBG_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))debug.o)



build b: lib.o
dbg d: debug.o



lib.o: $(LIB_FILES)
	echo " linking $(PRJ_NAME) object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) $(FLAGS_LNK)

debug.o: $(DBG_FILES)
	echo " linking $(PRJ_NAME) debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) $(FLAGS_LNK)



.PHONY: $(LIB_FILES)
$(LIB_FILES):
	$(MAKE) -C $(dir $@)

.PHONY: $(DBG_FILES)
$(DBG_FILES):
	$(MAKE) dbg -C $(dir $@)



clean c:
	$(MAKE) _clean -s

_clean:: $(addsuffix _clean,$(DIRS))
	echo "cleaning $(PRJ_NAME) $(DIR)"
	rm -f *.o

.PHONY: $(addsuffix _clean,$(DIRS))
$(addsuffix _clean,$(DIRS)):
	$(MAKE) _clean -C $(dir $@)



.PHONY: test
test t:
	$(MAKE) dbg -s
	$(MAKE) _test -s

_test: $(addsuffix _test,$(DIRS))

.PHONY: $(addsuffix _test,$(DIRS))
$(addsuffix _test,$(DIRS)):
	$(MAKE) _test -C $(dir $@)

PRJ_DIR := $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



# Only directories that opt in by having a Makefile, so a stray scratch or
# docs directory can never become a build target.
DIRS := $(dir $(wildcard */Makefile))

LIB_FILES = $(addsuffix lib.o,$(DIRS))
DBG_FILES = $(addsuffix debug.o,$(DIRS))



build b: lib.o
dbg d: debug.o



lib.o: $(LIB_FILES)
	echo " linking $(PRJ_NAME) object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) $(FLAGS_LNK) $(FLAGS_EXTRA)

debug.o: $(DBG_FILES)
	echo " linking $(PRJ_NAME) debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) $(FLAGS_LNK) $(FLAGS_EXTRA)



# FORCE, not .PHONY. Both always run the sub-make, but a .PHONY target has no
# timestamp, so every parent link would rerun unconditionally. These stay real
# files, so the links above fire only when a child object actually changed.
FORCE:

$(LIB_FILES): FORCE
	$(MAKE) -C $(dir $@)

$(DBG_FILES): FORCE
	$(MAKE) dbg -C $(dir $@)



clean c:
	$(MAKE) _clean -s

_clean:: $(addsuffix _clean,$(DIRS))
	echo "cleaning $(PRJ_NAME) $(DIR)"
	rm -f *.o *.d

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

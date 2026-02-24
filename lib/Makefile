PRJ_DIR = $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



DIRS := $(wildcard */.)

LIB_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))lib.o)
DBG_FILES = $(foreach DIR,$(DIRS),$(dir $(DIR))debug.o)



build b: $(LIB_FULL_FILE)

dbg d: $(DBG_FULL_FILE)



$(LIB_FULL_FILE): $(LIB_FILE) $(NUM_FILE)
	echo " linking pi full object $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_PRD) -o $@ $^ -nostdlib

$(LIB_FILE): $(LIB_FILES)
	echo " linking pi object $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_PRD) -o $@ $^ -nostdlib

$(DBG_FULL_FILE): $(DBG_FILE) $(NUM_DBG_FILE)  $(CLU_FILE)
	echo " linking pi full debug $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_DBG)  -o $@ $^ -nostdlib

$(DBG_FILE): $(DBG_FILES)
	echo " linking pi debug $(DIR)"
	gcc -r $(FLAGS) $(FLAGS_DBG)  -o $@ $^ -nostdlib



.PHONY: $(LIB_FILES) $(NUM_FILE) $(CLU_FILE)
$(LIB_FILES) $(NUM_FILE) $(CLU_FILE):
	$(MAKE) --directory=$(dir $@)

.PHONY: $(DBG_FILES) $(NUM_DBG_FILE)
$(DBG_FILES) $(NUM_DBG_FILE):
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

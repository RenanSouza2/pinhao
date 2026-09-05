PRJ_DIR := $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



build b: lib.o
dbg d: debug.o



# $< not $^: the .d files below add every header to these targets, and a
# compile takes exactly one input.
lib.o: code.c
	echo "building $(PRJ_NAME) object $(DIR)"
	gcc -o $@ $< $(FLAGS) $(FLAGS_PRD) $(FLAGS_CMP) $(FLAGS_EXTRA)

debug.o: code.c
	echo "building $(PRJ_NAME) debug $(DIR)"
	gcc -o $@ $< $(FLAGS) $(FLAGS_DBG) $(FLAGS_CMP) $(FLAGS_EXTRA)

-include lib.d debug.d



clean c:
	$(MAKE) clean --directory=$(LIB_DIR) -s -j

_clean:
	echo "cleaning $(PRJ_NAME) $(DIR)"
	rm -f *.o *.d
	$(MAKE) clean --directory=test -j



.PHONY: test
test t:
	$(MAKE) dbg --directory=$(LIB_DIR) -s -j
	$(MAKE) _test -s

_test:
	$(MAKE) --directory=test

export

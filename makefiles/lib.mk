PRJ_DIR = $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



build b: lib.o

dbg d: debug.o



lib.o: code.c
	echo "building $(PRJ_NAME) object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) -c

debug.o: code.c
	echo "building $(PRJ_NAME) debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) -c



clean c:
	$(MAKE) clean --directory=$(LIB_DIR) -s

_clean:
	echo "cleaning $(PRJ_NAME) $(DIR)"
	rm -f *.o
	$(MAKE) clean --directory=test



.PHONY: test
test t:
	$(MAKE) dbg --directory=$(LIB_DIR) -s
	$(MAKE) _test -s

_test:
	$(MAKE) --directory=test

export

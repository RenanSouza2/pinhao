PRJ_DIR = $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



build b: main.o

dbg d: debug.o



main.o: main.c $(LIB_FULL_FILE)
	echo "building pi exec $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD)

debug.o: main.c $(DBG_FULL_FILE)
	echo "building pi debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG)



.PHONY: $(LIB_FULL_FILE)
$(LIB_FULL_FILE):
	$(MAKE) --directory=$(dir $@)

.PHONY: $(DBG_FULL_FILE)
$(DBG_FULL_FILE):
	$(MAKE) dbg --directory=$(dir $@)



clean:
	echo "cleaning pi $(DIR)"
	rm -rf *.o
	$(MAKE) clean --directory=$(LIB_DIR)

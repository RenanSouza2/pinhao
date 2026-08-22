PRJ_DIR := $(shell git rev-parse --show-toplevel)
include $(PRJ_DIR)/makefiles/flags.mk
include $(PRJ_DIR)/makefiles/vars.mk



build b: main.out
dbg d: debug.out



main.out: main.o $(LIB_FULL_FILE)
	echo " linking $(PRJ_NAME) exec $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) $(FLAGS_EXE) $(FLAGS_EXTRA)

debug.out: debug.o $(DBG_FULL_FILE)
	echo " linking $(PRJ_NAME) debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) $(FLAGS_EXE) $(FLAGS_EXTRA)



main.o: main.c
	echo "building $(PRJ_NAME) object $(DIR) main"
	gcc -o $@ $< $(FLAGS) $(FLAGS_PRD) $(FLAGS_CMP) $(FLAGS_EXTRA)

debug.o: main.c
	echo "building $(PRJ_NAME) object $(DIR) debug"
	gcc -o $@ $< $(FLAGS) $(FLAGS_DBG) $(FLAGS_CMP) $(FLAGS_EXTRA)

-include main.d debug.d



# FORCE, not .PHONY; see the note in makefiles/bundle.mk.
FORCE:

$(LIB_FULL_FILE): FORCE
	$(MAKE) --directory=$(dir $@)

$(DBG_FULL_FILE): FORCE
	$(MAKE) dbg --directory=$(dir $@)



clean c:
	echo "cleaning $(PRJ_NAME) $(DIR)"
	rm -rf *.out *.o *.d *.dSYM
	$(MAKE) clean --directory=$(LIB_DIR)

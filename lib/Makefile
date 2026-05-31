include $(shell git rev-parse --show-toplevel)/makefiles/bundle.mk


build: $(LIB_FULL_FILE)
dbg: $(DBG_FULL_FILE)



$(LIB_FULL_FILE): lib.o $(NUM_FILE)
	echo " linking $(PRJ_NAME) full object $(DIR)"
	gcc $(FLAGS) $(FLAGS_PRD) $(FLAGS_LNK) -o $@ $^

$(DBG_FULL_FILE): debug.o $(NUM_DBG_FILE) $(CLU_FILE)
	echo " linking $(PRJ_NAME) full debug $(DIR)"
	gcc $(FLAGS) $(FLAGS_DBG) $(FLAGS_LNK) -o $@ $^



.PHONY: $(NUM_FILE) $(CLU_FILE)
$(NUM_FILE) $(CLU_FILE):
	$(MAKE) --directory=$(dir $@)

.PHONY: $(NUM_DBG_FILE)
$(NUM_DBG_FILE):
	$(MAKE) dbg --directory=$(dir $@)



_clean::
	$(MAKE) clean --directory $(dir ${CLU_FILE}) 
	$(MAKE) clean --directory $(dir ${NUM_FILE})

include $(shell git rev-parse --show-toplevel)/makefiles/bundle.mk


build: $(LIB_FULL_FILE)
dbg: $(DBG_FULL_FILE)



$(LIB_FULL_FILE): lib.o $(AUC_FILE)
	echo " linking $(PRJ_NAME) full object $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_PRD) $(FLAGS_LNK) $(FLAGS_EXTRA)

$(DBG_FULL_FILE): debug.o $(AUC_DBG_FILE) $(CLU_FILE)
	echo " linking $(PRJ_NAME) full debug $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG) $(FLAGS_LNK) $(FLAGS_EXTRA)



# FORCE comes from bundle.mk; see the note there on why not .PHONY.
$(AUC_FILE) $(CLU_FILE): FORCE
	$(MAKE) --directory=$(dir $@)

$(AUC_DBG_FILE): FORCE
	$(MAKE) dbg --directory=$(dir $@)



_clean::
	$(MAKE) clean --directory=$(dir $(CLU_FILE))
	$(MAKE) clean --directory=$(dir $(AUC_FILE))

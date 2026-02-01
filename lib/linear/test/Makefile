DEBUG_FILE = $(LIB_ROOT)/debug.o

test: runner
	echo "running test $(DIR)"
	./runner

runner: test.c $(DEBUG_FILE)
	echo "building pi test $(DIR)"
	gcc $(FLAGS) $(FLAGS_DBG) -o $@ $^

clean:
	rm -rf runner

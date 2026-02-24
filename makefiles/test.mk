test: runner
	echo "running test $(DIR)"
	./runner

runner: test.c $(DBG_FULL_FILE)
	echo "building test $(DIR)"
	gcc -o $@ $^ $(FLAGS) $(FLAGS_DBG)

clean:
	rm -rf runner

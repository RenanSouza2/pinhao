set -e
make dbg
time ./src/debug.o $@

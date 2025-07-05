set -e
rm -rf thread_log/*
make dbg
time ./src/debug.o $@

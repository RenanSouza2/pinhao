set -e
rm -rf thread_log/*
make build
time ./src/main.o $@

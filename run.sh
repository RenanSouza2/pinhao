set -e
make build
time ./src/main.o $@

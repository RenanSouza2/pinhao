# introduction

This is a template repository for c projects

It should be easy to use, easy to compose libraries, maintain a test routine

Feedbacks are appreciated

# commands

- `./run.sh <argments>`: build, if necessary, and run the project main file located at ./src
- `make` / `make build` / `make b`: build main executable
- `make clean` / `make c`: clean all object files
- `make test` / `make t`: build libs in debug mode and run all tests

# Structure

## lib

The lib folder has a makefile that compiles every folder inside it and links it, this can be done recursively

## lib example

Inside each lib there are some files:

- header.h: export functions with opaque types
- struct.h: where structs for this lib should be defined and also functions that don't treat these structs as opaque
- debug.h: export internal functions and also functions that only exist in debug mode
- code.c: where code exists
- test/test.c where tests are implemented for this lib
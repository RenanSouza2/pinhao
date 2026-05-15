FLAGS = -std=c23 -Wall -Wextra -Wpedantic -Werror -Wfatal-errors -Wshadow -Wpointer-arith -Wcast-qual -Wwrite-strings -Wundef -Wformat=2 -Wformat-signedness -Wnull-dereference -Wconversion -Wsign-conversion -D_POSIX_C_SOURCE=200809L -Wimplicit-fallthrough -Wfloat-equal -Wredundant-decls -Wdouble-promotion -Wmissing-include-dirs -Wswitch-enum -Wnested-externs -Wcast-align -Wmissing-prototypes -Walloc-zero -Wdate-time -Wmissing-declarations

FLAGS_PRD = -Ofast -march=native -flto=auto -fuse-linker-plugin -ffunction-sections -fdata-sections -funroll-loops
FLAGS_DBG = -D DEBUG -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined -fno-optimize-sibling-calls

FLAGS_CMP = -c
FLAGS_LNK = -r -nostdlib
FLAGS_EXE = 

ifeq ($(shell uname -s),Linux)
	FLAGS += -fanalyzer -Wduplicated-cond -Wduplicated-branches -Wlogical-op

    FLAGS_PRD += 
	FLAGS_DBG += -fsanitize=leak

	FLAGS_CMP += 
	FLAGS_EXE += -Wl,-z,defs -Wl,--gc-sections
endif

ifeq ($(shell uname -s),Darwin)
    FLAGS += -Wunreachable-code -Wunreachable-code-break -Wconditional-uninitialized -Wmissing-variable-declarations

    FLAGS_EXE += -Wl,-fatal_warnings -Wl,-dead_strip
endif

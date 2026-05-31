FLAGS = -std=c23 -Wall -Wextra -Wpedantic -Werror -Wfatal-errors -Wshadow -Wpointer-arith -Wcast-qual -Wwrite-strings -Wundef -Wformat=2 -Wformat-signedness -Wnull-dereference -Wconversion -Wsign-conversion -D_POSIX_C_SOURCE=200809L -Wimplicit-fallthrough -Wfloat-equal -Wredundant-decls -Wdouble-promotion -Wmissing-include-dirs -Wswitch-enum -Wnested-externs -Wmissing-prototypes -Wdate-time -Wmissing-declarations -Walloca -Wvla

FLAGS_PRD = -O2 -march=native -fstack-protector-strong -D_FORTIFY_SOURCE=3 -ffunction-sections -fdata-sections -flto
FLAGS_DBG = -D DEBUG -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined -fno-optimize-sibling-calls

FLAGS_CMP = -c
FLAGS_LNK = -r -nostdlib
FLAGS_EXE =

ifeq ($(shell uname -s),Linux)
	FLAGS += -fanalyzer -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wcast-align=strict -Walloc-zero -Wtrailing-whitespace -Wleading-whitespace=spaces

    FLAGS_PRD += -fstack-clash-protection -fcf-protection=full
	FLAGS_DBG += -fsanitize=leak

	FLAGS_CMP += -fPIE
	FLAGS_EXE += -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,defs -Wl,--gc-sections -Wl,--fatal-warnings
endif

ifeq ($(shell uname -s),Darwin)
    FLAGS += -Wunreachable-code -Wunreachable-code-break -Wconditional-uninitialized -Wmissing-variable-declarations -Wcast-align -Wshadow-all -Wassign-enum -Wcomma -Wcovered-switch-default -Wthread-safety -Wconsumed

    FLAGS_EXE += -Wl,-fatal_warnings -Wl,-dead_strip
endif

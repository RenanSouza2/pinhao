PRJ_NAME = pi

# Immediate (:=) so git runs once per make rather than on every expansion, and
# unconditional (not ?=) so an inherited PRJ_DIR from a parent project's
# sub-make cannot win and silently point at the wrong project.
PRJ_DIR := $(shell git rev-parse --show-toplevel)

DIR := $(patsubst $(PRJ_DIR)/%,%,$(CURDIR))

LIB_DIR = $(PRJ_DIR)/lib
AUC_DIR = $(PRJ_DIR)/mods/araucaria/lib
CLU_DIR = $(PRJ_DIR)/mods/clu/bin

LIB_FILE = $(LIB_DIR)/lib.o
DBG_FILE = $(LIB_DIR)/debug.o

AUC_FILE = $(AUC_DIR)/lib.o
AUC_DBG_FILE = $(AUC_DIR)/debug.o

CLU_FILE = $(CLU_DIR)/clu.o

LIB_FULL_FILE = $(LIB_DIR)/lib_full.o
DBG_FULL_FILE = $(LIB_DIR)/debug_full.o

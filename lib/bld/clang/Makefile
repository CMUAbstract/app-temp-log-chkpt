EXEC = libtemplog.a.bc

OBJECTS = \
	print.o \

# Makefile common across the binary and the library
include ../../../Makefile.common

include $(MAKER_ROOT)/Makefile.clang

CFLAGS += \
	-I../../src/include/libtemplog \
	-I$(WISP_BASE_ROOT) \
	-I$(LIBIO_ROOT)/src/include \

VPATH = ../../src

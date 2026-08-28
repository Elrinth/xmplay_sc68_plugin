# xmp-sc68 — native XMPlay input plugin (official sc68, not a Winamp wrap)
#
#   /usr/bin/make          # host tests + 32-bit DLL
#   /usr/bin/make dll      # dist/xmp-sc68.dll
#   /usr/bin/make test     # host render/seek tests
#   /usr/bin/make pack     # /workspace/xmp-sc68-1.0.1.zip
#
# If `make` is a wrapper, invoke GNU make explicitly.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DIST := $(ROOT)/dist
SRC  := $(ROOT)/src
INC  := $(ROOT)/include/xmplay
TP   := $(ROOT)/third_party
SC68 := $(TP)/sc68
CFG  := $(TP)/config
OBJ  := $(DIST)/obj
OBJW := $(DIST)/obj-i686

I686_HOST := i686-w64-mingw32
I686_CC   := $(I686_HOST)-gcc
I686_CXX  := $(I686_HOST)-g++

JOBS    ?= $(shell nproc 2>/dev/null || echo 2)
GNUMAKE := /usr/bin/make

UNICE_SRCS = \
	$(SC68)/unice68/unice68_unpack.c \
	$(SC68)/unice68/unice68_version.c

FILE68_SRCS = \
	$(SC68)/file68/src/endian68.c \
	$(SC68)/file68/src/error68.c \
	$(SC68)/file68/src/file68.c \
	$(SC68)/file68/src/gzip68.c \
	$(SC68)/file68/src/ice68.c \
	$(SC68)/file68/src/init68.c \
	$(SC68)/file68/src/msg68.c \
	$(SC68)/file68/src/option68.c \
	$(SC68)/file68/src/registry68.c \
	$(SC68)/file68/src/replay68.c \
	$(SC68)/file68/src/rsc68.c \
	$(SC68)/file68/src/string68.c \
	$(SC68)/file68/src/timedb68.c \
	$(SC68)/file68/src/uri68.c \
	$(SC68)/file68/src/vfs68.c \
	$(SC68)/file68/src/vfs68_ao.c \
	$(SC68)/file68/src/vfs68_curl.c \
	$(SC68)/file68/src/vfs68_fd.c \
	$(SC68)/file68/src/vfs68_file.c \
	$(SC68)/file68/src/vfs68_mem.c \
	$(SC68)/file68/src/vfs68_null.c \
	$(SC68)/file68/src/vfs68_z.c

LIBSC68_SRCS = \
	$(SC68)/libsc68/src/api68.c \
	$(SC68)/libsc68/src/conf68.c \
	$(SC68)/libsc68/src/libsc68.c \
	$(SC68)/libsc68/src/mixer68.c \
	$(SC68)/libsc68/emu68/emu68.c \
	$(SC68)/libsc68/emu68/error68.c \
	$(SC68)/libsc68/emu68/getea68.c \
	$(SC68)/libsc68/emu68/inst68.c \
	$(SC68)/libsc68/emu68/ioplug68.c \
	$(SC68)/libsc68/emu68/lines68.c \
	$(SC68)/libsc68/emu68/mem68.c \
	$(SC68)/libsc68/io68/io68.c \
	$(SC68)/libsc68/io68/mfp_io.c \
	$(SC68)/libsc68/io68/mfpemul.c \
	$(SC68)/libsc68/io68/mw_io.c \
	$(SC68)/libsc68/io68/mwemul.c \
	$(SC68)/libsc68/io68/paula_io.c \
	$(SC68)/libsc68/io68/paulaemul.c \
	$(SC68)/libsc68/io68/shifter_io.c \
	$(SC68)/libsc68/io68/ym_blep.c \
	$(SC68)/libsc68/io68/ym_dump.c \
	$(SC68)/libsc68/io68/ym_envel.c \
	$(SC68)/libsc68/io68/ym_io.c \
	$(SC68)/libsc68/io68/ym_puls.c \
	$(SC68)/libsc68/io68/ymemul.c \
	$(SC68)/libsc68/dial68/dial68.c \
	$(SC68)/libsc68/dial68/dial_conf.c \
	$(SC68)/libsc68/dial68/dial_finf.c \
	$(SC68)/libsc68/dial68/dial_tsel.c

LIBC68_SRCS = \
	$(SC68)/sc68-libc/basename.c

SC68_SRCS = $(UNICE_SRCS) $(FILE68_SRCS) $(LIBSC68_SRCS)

INCS = \
	-I$(CFG) \
	-I$(SC68)/unice68 \
	-I$(SC68)/file68 \
	-I$(SC68)/file68/src \
	-I$(SC68)/file68/sc68 \
	-I$(SC68)/libsc68 \
	-I$(SC68)/libsc68/src \
	-I$(SC68)/libsc68/sc68 \
	-I$(SC68)/libsc68/emu68 \
	-I$(SC68)/libsc68/io68 \
	-I$(SC68)/libsc68/dial68 \
	-I$(SC68)/sc68-libc \
	-I$(SRC)

DEFS = -DHAVE_CONFIG_H -DEMU68_MONOLITIC -DEMU68_EXPORT -DUSE_REPLAY68 \
	-DNDEBUG -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_STRING_H

CFLAGS_COM = -O2 -fno-strict-aliasing -Wno-unused-function -Wno-unused-parameter \
	-Wno-implicit-function-declaration -Wno-incompatible-pointer-types \
	-Wno-int-conversion -Wno-discarded-qualifiers -Wno-format \
	$(INCS) $(DEFS)

# Linux host
CFLAGS_L = $(CFLAGS_COM) -fPIC -DHAVE_BASENAME -DHAVE_LIBGEN_H -DHAVE_UNISTD_H
# MinGW i686
CFLAGS_W = $(CFLAGS_COM) -DWIN32 -D_WIN32

LINUX_OBJS = $(patsubst $(SC68)/%.c,$(OBJ)/%.o,$(SC68_SRCS)) \
	$(OBJ)/sc68_player.o
WIN_OBJS = $(patsubst $(SC68)/%.c,$(OBJW)/%.o,$(SC68_SRCS) $(LIBC68_SRCS)) \
	$(OBJW)/sc68_player.o

.PHONY: all dll test pack clean

all: test dll

dll: $(DIST)/xmp-sc68.dll

test: $(DIST)/test_sc68_render
	$(DIST)/test_sc68_render

$(OBJ)/%.o: $(SC68)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_L) -c -o $@ $<

$(OBJ)/sc68_player.o: $(SRC)/sc68_player.c $(SRC)/sc68_player.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_L) -c -o $@ $<

$(OBJW)/%.o: $(SC68)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_W) -c -o $@ $<

$(OBJW)/sc68_player.o: $(SRC)/sc68_player.c $(SRC)/sc68_player.h
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_W) -c -o $@ $<

$(DIST)/test_sc68_render: $(ROOT)/tests/test_sc68_render.c $(LINUX_OBJS)
	mkdir -p $(DIST)
	$(CC) $(CFLAGS_L) -o $@ $(ROOT)/tests/test_sc68_render.c $(LINUX_OBJS) -lz -lm

$(DIST)/xmp-sc68.dll: $(SRC)/xmp-sc68.cpp $(SRC)/xmp-sc68.def $(WIN_OBJS)
	mkdir -p $(DIST)
	$(I686_CXX) -shared -O2 -DNDEBUG -std=c++14 \
	  -static -static-libgcc -static-libstdc++ \
	  -I$(INC) $(INCS) $(DEFS) -DWIN32 -D_WIN32 \
	  -o $@ $(SRC)/xmp-sc68.cpp $(SRC)/xmp-sc68.def $(WIN_OBJS) \
	  -Wl,--kill-at -Wl,--add-stdcall-alias \
	  -lz -lshlwapi -lwinmm -luser32 -lgdi32 -Wl,-s
	$(I686_HOST)-objdump -p $@ | grep -E 'dll name|XMPIN_GetInterface|file format' || true
	file $@

pack: dll
	rm -f /workspace/xmp-sc68-1.0.1.zip
	mkdir -p $(DIST)/pack
	cp -f $(DIST)/xmp-sc68.dll $(ROOT)/README.md $(DIST)/pack/
	cd $(DIST)/pack && zip -9 /workspace/xmp-sc68-1.0.1.zip xmp-sc68.dll README.md
	rm -rf $(DIST)/pack
	ls -l /workspace/xmp-sc68-1.0.zip

clean:
	rm -rf $(DIST)/xmp-sc68.dll $(DIST)/test_sc68_render $(DIST)/obj $(DIST)/obj-i686 $(DIST)/pack

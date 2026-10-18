# Comic Reader for webOS PDK

WEBOS_PDK ?= /opt/PalmPDK
TOOLCHAIN = $(WEBOS_PDK)/arm-toolchain/bin

CC = $(TOOLCHAIN)/arm-none-linux-gnueabi-gcc
STRIP = $(TOOLCHAIN)/arm-none-linux-gnueabi-strip

APP_NAME = comic-reader
APP_ID = org.webos.comicreader

# Compiler flags
CFLAGS = -O2 -Wall -std=gnu99
CFLAGS += -I$(WEBOS_PDK)/include
CFLAGS += -I$(WEBOS_PDK)/include/SDL
CFLAGS += -Iminizip
CFLAGS += -Iunarr
CFLAGS += -DIOAPI_NO_64
CFLAGS += -DHAVE_ZLIB

# Linker flags
LDFLAGS = -L$(WEBOS_PDK)/device/lib
LDFLAGS += -Wl,--allow-shlib-undefined

# Libraries
LIBS = -lSDL -lSDL_ttf -lSDL_image -lpdl -lz -lcurl -lssl -lcrypto

# Source files
SRC = src/main.c src/cbz.c src/cache.c src/ui.c src/webdav.c src/config.c src/xml_parser.c
SRC += minizip/unzip.c minizip/ioapi.c

# unarr sources for CBR support
SRC += unarr/common/stream.c unarr/common/unarr.c unarr/common/crc32.c
SRC += unarr/common/conv.c unarr/common/custalloc.c
SRC += unarr/rar/rar.c unarr/rar/parse-rar.c unarr/rar/uncompress-rar.c
SRC += unarr/rar/huffman-rar.c unarr/rar/filter-rar.c unarr/rar/rarvm.c
# LZMA SDK for RAR decompression
SRC += unarr/lzmasdk/LzmaDec.c unarr/lzmasdk/Ppmd7.c unarr/lzmasdk/Ppmd7Dec.c
SRC += unarr/lzmasdk/Ppmd7aDec.c unarr/lzmasdk/Ppmd8.c unarr/lzmasdk/Ppmd8Dec.c
SRC += unarr/lzmasdk/CpuArch.c

OBJ = $(SRC:.c=.o)

TARGET = $(APP_NAME)

.PHONY: all clean package install

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET) *.ipk

package: $(TARGET)
	palm-package .

install: package
	palm-install $(APP_ID)_*.ipk

# Dependencies
src/main.o: src/main.c src/ui.h src/cbz.h src/cache.h
src/cbz.o: src/cbz.c src/cbz.h minizip/unzip.h unarr/unarr.h
src/cache.o: src/cache.c src/cache.h src/cbz.h
src/ui.o: src/ui.c src/ui.h src/cbz.h src/cache.h

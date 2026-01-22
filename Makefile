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
CFLAGS += -DIOAPI_NO_64

# Linker flags
LDFLAGS = -L$(WEBOS_PDK)/device/lib
LDFLAGS += -Wl,--allow-shlib-undefined

# Libraries
LIBS = -lSDL -lSDL_ttf -lSDL_image -lpdl -lz

# Source files
SRC = src/main.c src/cbz.c src/cache.c src/ui.c
SRC += minizip/unzip.c minizip/ioapi.c
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
src/cbz.o: src/cbz.c src/cbz.h minizip/unzip.h
src/cache.o: src/cache.c src/cache.h src/cbz.h
src/ui.o: src/ui.c src/ui.h src/cbz.h src/cache.h

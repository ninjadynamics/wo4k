CC = gcc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra

ifeq ($(OS),Windows_NT)
TARGET = deband.exe
LIBS = -lpng -ljpeg -lz -lm
CFLAGS += -D_CRT_SECURE_NO_WARNINGS
else
TARGET = deband
LIBS = -lpng -ljpeg -lz -lm -pthread
CFLAGS += -D_XOPEN_SOURCE=700
endif

all: $(TARGET)

static:
	$(CC) $(CFLAGS) -static -static-libgcc -o $(TARGET) deband.c pngtrim.c $(LIBS)

$(TARGET): deband.c pngtrim.c pngtrim.h
	$(CC) $(CFLAGS) -o $@ deband.c pngtrim.c $(LIBS)

clean:
	rm -f deband deband.exe

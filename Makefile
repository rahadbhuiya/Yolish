#  Yolish v2.4 — Makefile
#  Targets: all  debug  windows  icons  release  clean

CC       ?= gcc
MINGW_CC  = x86_64-w64-mingw32-gcc
WINDRES   = x86_64-w64-mingw32-windres
RSVG      = rsvg-convert
CONVERT   = convert

CFLAGS    = -std=c11 -Wall -O2
SAN       = -fsanitize=address,undefined -fno-omit-frame-pointer -g

SRCS      = lexer.c parser.c eval.c compiler.c \
            elf_out.c pe_out.c macho_out.c formatter.c checker.c main.c

ICONS_DIR = icons

#  Detect host OS 
ifeq ($(OS),Windows_NT)
    EXE    = ys.exe
    LIBS   =
    CFLAGS += -D_WIN32
else
    UNAME := $(shell uname -s)
    EXE    = ys
    LIBS   = -lm
endif

#  all: build for host OS 
all: $(EXE)

$(EXE): $(SRCS) yolish.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)
	@echo "Built: ./$(EXE)"

#  debug: address + UB sanitizers 
debug: $(SRCS) yolish.h
	$(CC) $(CFLAGS) $(SAN) -o ys_debug $(SRCS) $(LIBS)
	@echo "Debug build: ./ys_debug"

#  icons: generate PNG sizes + ICO from SVG 
icons: $(ICONS_DIR)/logo.svg
	$(RSVG) -w 16  -h 16  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_16.png
	$(RSVG) -w 32  -h 32  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_32.png
	$(RSVG) -w 48  -h 48  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_48.png
	$(RSVG) -w 64  -h 64  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_64.png
	$(RSVG) -w 128 -h 128 $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_128.png
	$(RSVG) -w 256 -h 256 $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_256.png
	$(CONVERT) $(ICONS_DIR)/logo_16.png  $(ICONS_DIR)/logo_32.png  \
	           $(ICONS_DIR)/logo_48.png  $(ICONS_DIR)/logo_64.png  \
	           $(ICONS_DIR)/logo_128.png $(ICONS_DIR)/logo_256.png \
	           -colors 256 $(ICONS_DIR)/ys.ico
	@echo "Icons generated → $(ICONS_DIR)/ys.ico"

#  windows: cross-compile PE32+ .exe (requires MinGW) 
#   Run `make icons` first if icons/ys.ico does not exist yet.
windows: $(ICONS_DIR)/ys.ico ys_icon.o
	$(MINGW_CC) $(CFLAGS) -static -o ys.exe $(SRCS) ys_icon.o -lm
	@echo "Built: ./ys.exe (Windows PE32+)"

ys_icon.o: ys_icon.rc $(ICONS_DIR)/ys.ico
	$(WINDRES) ys_icon.rc -O coff -o ys_icon.o

#  release: build all three platform binaries 
release: all windows
	@echo "--------------------------------------------"
	@echo "Release build complete"
	@echo "  Linux/macOS : ./$(EXE)"
	@echo "  Windows     : ./ys.exe"
	@echo "--------------------------------------------"

#  clean 
clean:
	rm -f ys ys.exe ys_debug ys_icon.o *.o \
	      $(ICONS_DIR)/logo_*.png $(ICONS_DIR)/ys.ico

.PHONY: all debug icons windows release clean
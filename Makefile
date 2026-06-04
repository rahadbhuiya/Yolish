# Yolish v1.0 Makefile
# Builds on Linux, macOS, Windows (MinGW/MSVC)
CC        ?= gcc
MINGW_CC   = x86_64-w64-mingw32-gcc
WINDRES    = x86_64-w64-mingw32-windres
RSVG       = rsvg-convert
CONVERT    = convert
CFLAGS     = -std=c11 -Wall -O2
SAN        = -fsanitize=address,undefined -fno-omit-frame-pointer -g
SRCS       = lexer.c parser.c eval.c compiler.c elf_out.c pe_out.c macho_out.c main.c
ICONS_DIR  = icons

# Detect host OS
ifeq ($(OS),Windows_NT)
    EXE    = ys.exe
    LIBS   =
    CFLAGS += -D_WIN32
else
    UNAME := $(shell uname -s)
    EXE    = ys
    LIBS   = -lm
endif

#  Default: build for host OS 
all: $(EXE)
$(EXE): $(SRCS) yolish.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)
	@echo "Built: ./$(EXE)"

#  Debug build 
debug: $(SRCS) yolish.h
	$(CC) $(CFLAGS) $(SAN) -o ys_debug $(SRCS) $(LIBS)
	@echo "Debug build: ./ys_debug"

#  Cross-compile Windows .exe 
windows: $(ICONS_DIR)/ys.ico ys_icon.o
	$(MINGW_CC) $(CFLAGS) -static -o ys.exe $(SRCS) ys_icon.o -lm
	@echo "Built: ./ys.exe (Windows PE32+)"

ys_icon.o: ys_icon.rc $(ICONS_DIR)/ys.ico
	$(WINDRES) ys_icon.rc -O coff -o ys_icon.o

#  Generate icon assets from SVG 
icons: $(ICONS_DIR)/logo.svg
	$(RSVG) -w 16  -h 16  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_16.png
	$(RSVG) -w 32  -h 32  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_32.png
	$(RSVG) -w 48  -h 48  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_48.png
	$(RSVG) -w 64  -h 64  $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_64.png
	$(RSVG) -w 128 -h 128 $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_128.png
	$(RSVG) -w 256 -h 256 $(ICONS_DIR)/logo.svg -o $(ICONS_DIR)/logo_256.png
	$(CONVERT) $(ICONS_DIR)/logo_16.png $(ICONS_DIR)/logo_32.png \
	           $(ICONS_DIR)/logo_48.png $(ICONS_DIR)/logo_64.png \
	           $(ICONS_DIR)/logo_128.png $(ICONS_DIR)/logo_256.png \
	           -colors 256 $(ICONS_DIR)/logo.ico
	@echo "Icons generated"

#  Cross-compile Windows .exe 
windows: $(ICONS_DIR)/ys.ico ys_icon.o
	$(MINGW_CC) $(CFLAGS) -static -o ys.exe $(SRCS) ys_icon.o -lm
	@echo "Built: ./ys.exe (Windows PE32+)"

#  Clean 
clean:
	rm -f ys ys.exe ys_debug ys_icon.o *.o \
	      $(ICONS_DIR)/logo_*.png $(ICONS_DIR)/logo.ico
.PHONY: all debug windows icons release clean
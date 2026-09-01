#  Yolish v2.36 — Makefile
#  Targets: all  debug  windows  icons  release  clean

CC       ?= gcc
MINGW_CC  = x86_64-w64-mingw32-gcc
WINDRES   = x86_64-w64-mingw32-windres
RSVG      = rsvg-convert
CONVERT   = convert

CFLAGS    = -std=c11 -Wall -O2
SAN       = -fsanitize=address,undefined -fno-omit-frame-pointer -g

SRCS      = lexer.c parser.c eval.c net_runtime.c compiler.c \
            elf_out.c pe_out.c macho_out.c formatter.c checker.c \
            bytecode.c bcompiler.c vm.c main.c
# NOTE: compiler_net.c is NOT listed above on purpose (v2.34) -- it's
# #include-d directly into compiler.c, not compiled as its own
# translation unit. Adding it here would define every symbol in it
# twice and fail to link.

ICONS_DIR = icons

#  Detect host OS 
ifeq ($(OS),Windows_NT)
    EXE    = ys.exe
    LIBS   = -lws2_32
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

#  tls: build with real TLS support (y.net.tls_*), via OpenSSL.
#   Linux/macOS only for now — requires libssl-dev (Debian/Ubuntu:
#   `apt install libssl-dev`, macOS: `brew install openssl`, then you
#   may need to pass -I/-L flags pointing at brew's openssl if it's
#   not on the default search path). This is a separate target, not
#   folded into `all`, specifically so the default build (including
#   Windows CI) keeps working without needing OpenSSL set up.
#   macOS + Homebrew note: if the linker can't find -lssl/-lcrypto,
#   Homebrew's openssl isn't symlinked onto the default search path
#   (common on Apple Silicon). Find it with `brew --prefix openssl`
#   and add e.g.:
#     make tls CFLAGS="$(CFLAGS) -I$(brew --prefix openssl)/include" \
#              LDFLAGS="-L$(brew --prefix openssl)/lib"
tls: $(SRCS) yolish.h
	$(CC) $(CFLAGS) -DYS_WITH_TLS $(LDFLAGS) -o $(EXE) $(SRCS) $(LIBS) -lssl -lcrypto
	@echo "Built: ./$(EXE) (with TLS support — y.net.tls_* is live)"

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
	$(MINGW_CC) $(CFLAGS) -static -o ys.exe $(SRCS) ys_icon.o -lm -lws2_32
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

.PHONY: all tls debug icons windows release clean
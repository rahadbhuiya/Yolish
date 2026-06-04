# Building Yolish

> **Note:** This guide is for **contributors** who want to build Yolish from source.  
> If you just want to use Yolish, download the binary from [Releases](../../releases/latest) — no compiler needed.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Build on Linux](#build-on-linux)
3. [Build on macOS](#build-on-macos)
4. [Build on Windows](#build-on-windows)
5. [Cross-compile ys.exe from Linux](#cross-compile-ysexe-from-linux)
6. [Build with Icon (Windows)](#build-with-icon-windows)
7. [Debug Build](#debug-build)
8. [Release Build (all platforms)](#release-build-all-platforms)
9. [Makefile Targets](#makefile-targets)
10. [GitHub Actions Auto-Release](#github-actions-auto-release)

---

## Prerequisites

| Platform | Compiler | Minimum version |
|----------|----------|----------------|
| Linux | gcc or clang | gcc 9+ / clang 10+ |
| macOS | clang (Xcode) | Xcode 12+ |
| Windows | MinGW-w64 or MSVC | gcc 9+ / VS 2019+ |
| Cross (Linux → Windows) | `gcc-mingw-w64-x86-64` | 9+ |

**Linux — install compiler:**
```bash
sudo apt install build-essential        # Debian/Ubuntu
sudo dnf install gcc                    # Fedora
sudo pacman -S gcc                      # Arch
```

**macOS — install compiler:**
```bash
xcode-select --install
```

**Windows — install MinGW:**
- Download from https://www.mingw-w64.org/
- Or install via [MSYS2](https://www.msys2.org/): `pacman -S mingw-w64-x86_64-gcc`
- Or install [w64devkit](https://github.com/skeeto/w64devkit/releases) (simplest)

---

## Build on Linux

```bash
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make
```

Output: `./ys`

```bash
./ys examples/hello.y          # interpret
./ys -c examples/hello.y       # compile native binary
./hello                         # run the compiled binary
```

Install system-wide:
```bash
sudo cp ys /usr/local/bin/
```

---

## Build on macOS

```bash
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make
```

Output: `./ys`

Install system-wide:
```bash
sudo cp ys /usr/local/bin/
# or
sudo cp ys /usr/bin/
```

---

## Build on Windows

**With MinGW (Command Prompt):**
```cmd
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make
```

Output: `ys.exe`

**Without make (manual):**
```cmd
gcc -std=c11 -O2 -o ys.exe ^
  lexer.c parser.c eval.c compiler.c ^
  elf_out.c pe_out.c macho_out.c main.c
```

**With MSVC (Developer Command Prompt):**
```cmd
cl /std:c11 /O2 /Fe:ys.exe ^
  lexer.c parser.c eval.c compiler.c ^
  elf_out.c pe_out.c macho_out.c main.c
```

Add to PATH:
```cmd
setx PATH "%PATH%;C:\path\to\yolish"
```

---

## Cross-compile ys.exe from Linux

Build a Windows `.exe` from Linux using MinGW:

```bash
# Install MinGW cross-compiler
sudo apt install gcc-mingw-w64-x86-64

# Build (no icon)
make windows

# Or manually
x86_64-w64-mingw32-gcc -std=c11 -O2 -static \
  -o ys.exe \
  lexer.c parser.c eval.c compiler.c \
  elf_out.c pe_out.c macho_out.c main.c -lm
```

Output: `./ys.exe` — a valid Windows PE32+ binary.

---

## Build with Icon (Windows)

Embeds the Yolish logo into `ys.exe` (shows in Explorer, taskbar, title bar).

**Requirements:**
```bash
sudo apt install gcc-mingw-w64-x86-64 mingw-w64-tools librsvg2-bin imagemagick
```

**Steps:**
```bash
# 1. Generate PNG sizes from SVG
make icons

# 2. Compile resource (icon + version info)
x86_64-w64-mingw32-windres ys_icon.rc -O coff -o ys_icon.o

# 3. Build ys.exe with icon embedded
x86_64-w64-mingw32-gcc -std=c11 -O2 -static \
  -o ys.exe \
  lexer.c parser.c eval.c compiler.c \
  elf_out.c pe_out.c macho_out.c main.c \
  ys_icon.o -lm
```

Or simply:
```bash
make windows    # does all three steps automatically
```

---

## Debug Build

Adds AddressSanitizer + UndefinedBehaviorSanitizer:

```bash
make debug
./ys_debug examples/hello.y
```

The debug binary is named `ys_debug` and includes:
- `-fsanitize=address,undefined` — catches memory errors and UB
- `-g` — debug symbols for GDB/LLDB
- `-fno-omit-frame-pointer` — proper stack traces

---

## Release Build (all platforms)

Builds both Linux and Windows binaries in one step:

```bash
# Requires: gcc + MinGW + rsvg-convert + imagemagick
make release
```

Output:
```
ys          Linux x86-64 binary
ys.exe      Windows x86-64 binary (with icon)
```

---

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` | Build `ys` for current OS |
| `make windows` | Cross-compile `ys.exe` for Windows |
| `make icons` | Generate `logo.ico` + PNG sizes from `logo.svg` |
| `make release` | Build both `ys` and `ys.exe` |
| `make debug` | Build `ys_debug` with sanitizers |
| `make clean` | Remove all build artifacts |

---

## GitHub Actions Auto-Release

Pushing a version tag triggers automatic builds for all platforms:

```bash
git tag v1.1
git push origin v1.1
```

GitHub Actions will:
1. Build `ys-linux` (Ubuntu runner, gcc static)
2. Cross-compile `ys.exe` (Ubuntu + MinGW, with icon)
3. Build `ys-macos` (macOS runner)
4. Create a GitHub Release with all three binaries attached

Workflow file: [`.github/workflows/release.yml`](.github/workflows/release.yml)

---


#!/bin/sh
# Yolish Installer for Linux / macOS / Exploidus
# Usage: curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh

set -e

VERSION="v1.0"
REPO="rahadbhuiya/yolish"

echo "=============================="
echo "  Yolish $VERSION Installer"
echo "=============================="

# Detect OS
OS=$(uname -s)

case "$OS" in
  Linux)  BINARY="ys-linux"  ;;
  Darwin) BINARY="ys-macos"  ;;
  *)
    echo "Unsupported OS: $OS"
    echo "Please build from source: https://github.com/$REPO"
    exit 1
    ;;
esac

# Pick install dir: prefer /usr/local/bin if writable, else ~/.local/bin
if [ -w "/usr/local/bin" ]; then
    INSTALL_DIR="/usr/local/bin"
elif [ "$(id -u)" = "0" ]; then
    INSTALL_DIR="/usr/local/bin"
else
    INSTALL_DIR="$HOME/.local/bin"
    mkdir -p "$INSTALL_DIR"
    echo "Note: installing to $INSTALL_DIR (no sudo)"
    echo "      Make sure $INSTALL_DIR is in your PATH."
    echo "      Add this to ~/.bashrc or ~/.zshrc if needed:"
    echo "        export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
fi

DEST="$INSTALL_DIR/ys"
URL="https://github.com/$REPO/releases/download/$VERSION/$BINARY"

echo "Downloading $BINARY..."
if command -v curl >/dev/null 2>&1; then
    curl -fSL "$URL" -o "$DEST"
elif command -v wget >/dev/null 2>&1; then
    wget -q "$URL" -O "$DEST"
else
    echo "Error: curl or wget required"
    exit 1
fi

chmod +x "$DEST"

echo ""
echo "=============================="
echo "  Yolish installed at $DEST"
echo "  Run: ys hello.y"
echo "       ys -c hello.y"
echo "=============================="
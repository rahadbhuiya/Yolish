#!/bin/sh
# Yolish Installer for Linux / macOS / Exploidus
# Usage: curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh

set -e

VERSION="v1.0"
REPO="rahadbhuiya/yolish"
INSTALL_DIR="/usr/local/bin"

echo "=============================="
echo "  Yolish $VERSION Installer"
echo "=============================="

# Detect OS
OS=$(uname -s)
ARCH=$(uname -m)

case "$OS" in
  Linux)
    BINARY="ys-linux"
    ;;
  Darwin)
    BINARY="ys-macos"
    ;;
  *)
    echo "Unsupported OS: $OS"
    echo "Please build from source: https://github.com/$REPO"
    exit 1
    ;;
esac

# Download binary
URL="https://github.com/$REPO/releases/download/$VERSION/$BINARY"
DEST="$INSTALL_DIR/ys"

echo "Downloading $BINARY..."
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o "$DEST"
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
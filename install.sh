#!/bin/sh
# Yolish Installer for Linux / macOS
# Usage: curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh

set -e

VERSION="v2.6"
REPO="rahadbhuiya/yolish"
RAW="https://raw.githubusercontent.com/$REPO/master"

echo "=============================="
echo "  Yolish $VERSION Installer"
echo "=============================="

#  Detect OS 
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

#  Pick install dir 
if [ -w "/usr/local/bin" ] || [ "$(id -u)" = "0" ]; then
    INSTALL_DIR="/usr/local/bin"
    ICON_DIR="/usr/share/icons/hicolor"
    MIME_DIR="/usr/share/mime"
    APP_DIR="/usr/share/applications"
    SYSTEM_INSTALL=1
else
    INSTALL_DIR="$HOME/.local/bin"
    ICON_DIR="$HOME/.local/share/icons/hicolor"
    MIME_DIR="$HOME/.local/share/mime"
    APP_DIR="$HOME/.local/share/applications"
    SYSTEM_INSTALL=0
    mkdir -p "$INSTALL_DIR"
    echo "Note: installing to $INSTALL_DIR (no sudo)"
    echo "      Add to PATH if needed:"
    echo "        export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
fi

#  Helper: download 
download() {
    URL="$1"
    DEST="$2"
    mkdir -p "$(dirname "$DEST")"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL "$URL" -o "$DEST"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$URL" -O "$DEST"
    else
        echo "Error: curl or wget required"
        exit 1
    fi
}

#  Step 1: Download ys binary 
echo "[1/4] Downloading $BINARY..."
DEST="$INSTALL_DIR/ys"
download "https://github.com/$REPO/releases/download/$VERSION/$BINARY" "$DEST"
chmod +x "$DEST"
echo "      Installed: $DEST"

#  Step 2: Install icons 
echo "[2/4] Installing icons..."

if [ "$OS" = "Linux" ]; then
    # Install logo.png at multiple sizes for the app icon
    for SIZE in 16 32 48 64 128 256; do
        mkdir -p "$ICON_DIR/${SIZE}x${SIZE}/apps"
        mkdir -p "$ICON_DIR/${SIZE}x${SIZE}/mimetypes"
    done

    # Download logo.svg and file.png from GitHub
    download "$RAW/icons/logo.svg" "/tmp/yolish_logo.svg"
    download "$RAW/icons/file.png" "/tmp/yolish_file.png"

    # Use rsvg-convert or convert to generate sizes
    if command -v rsvg-convert >/dev/null 2>&1; then
        CONVERTER="rsvg"
    elif command -v convert >/dev/null 2>&1; then
        CONVERTER="convert"
    else
        CONVERTER=""
        echo "      Note: install librsvg2-bin or imagemagick for best icon quality"
    fi

    for SIZE in 16 32 48 64 128 256; do
        APPICON="$ICON_DIR/${SIZE}x${SIZE}/apps/yolish.png"
        MIMEICON="$ICON_DIR/${SIZE}x${SIZE}/mimetypes/text-x-yolish.png"
        if [ "$CONVERTER" = "rsvg" ]; then
            rsvg-convert -w $SIZE -h $SIZE /tmp/yolish_logo.svg -o "$APPICON" 2>/dev/null
            rsvg-convert -w $SIZE -h $SIZE /tmp/yolish_file.png -o "$MIMEICON" 2>/dev/null || \
                cp "$APPICON" "$MIMEICON"
        elif [ "$CONVERTER" = "convert" ]; then
            convert /tmp/yolish_logo.svg -resize ${SIZE}x${SIZE} "$APPICON" 2>/dev/null
            convert /tmp/yolish_file.png -resize ${SIZE}x${SIZE} "$MIMEICON" 2>/dev/null
        else
            # Fallback: copy as-is to 256 only
            cp /tmp/yolish_logo.svg "$ICON_DIR/256x256/apps/yolish.svg" 2>/dev/null || true
            cp /tmp/yolish_file.png "$ICON_DIR/256x256/mimetypes/text-x-yolish.png" 2>/dev/null || true
            break
        fi
    done

    # Update icon cache (Linux only)
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
    fi
    echo "      Icons installed"

elif [ "$OS" = "Darwin" ]; then
    # macOS: install logo.png to app support
    mkdir -p "$HOME/Library/Application Support/Yolish/icons"
    download "$RAW/icons/logo.png" "$HOME/Library/Application Support/Yolish/icons/logo.png"
    download "$RAW/icons/file.png" "$HOME/Library/Application Support/Yolish/icons/file.png"
    echo "      Icons downloaded (macOS file association set in Step 4)"
fi

#  Step 3: Register MIME type (.y files) 
echo "[3/4] Registering .y file type..."

if [ "$OS" = "Linux" ]; then
    mkdir -p "$MIME_DIR/packages"

    cat > "$MIME_DIR/packages/text-x-yolish.xml" << 'XML'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="text/x-yolish">
    <comment>Yolish Source File</comment>
    <glob pattern="*.y"/>
    <icon name="text-x-yolish"/>
  </mime-type>
</mime-info>
XML

    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "$MIME_DIR" 2>/dev/null || true
    fi
    echo "      MIME type registered"

elif [ "$OS" = "Darwin" ]; then
    echo "      Skipped (macOS uses .plist — see Step 4)"
fi

#  Step 4: Desktop entry / file association 
echo "[4/4] Registering file association..."

if [ "$OS" = "Linux" ]; then
    mkdir -p "$APP_DIR"

    cat > "$APP_DIR/yolish.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=Yolish
Comment=Yolish Language Runtime
Exec=$DEST %f
Icon=yolish
Terminal=true
MimeType=text/x-yolish;
Categories=Development;
DESKTOP

    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$APP_DIR" 2>/dev/null || true
    fi
    if command -v xdg-mime >/dev/null 2>&1; then
        xdg-mime default yolish.desktop text/x-yolish 2>/dev/null || true
    fi
    echo "      .desktop entry registered"
    echo "      .y files will open with Yolish"

elif [ "$OS" = "Darwin" ]; then
    # macOS: write a LaunchServices plist
    PLIST="$HOME/Library/Application Support/Yolish/Yolish.app/Contents/Info.plist"
    mkdir -p "$(dirname "$PLIST")"
    ICON_PATH="$HOME/Library/Application Support/Yolish/icons/file.png"

    cat > "$PLIST" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>           <string>Yolish</string>
  <key>CFBundleIdentifier</key>     <string>com.exploidus.yolish</string>
  <key>CFBundleVersion</key>        <string>1.5</string>
  <key>CFBundleExecutable</key>     <string>ys</string>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeExtensions</key> <array><string>y</string></array>
      <key>CFBundleTypeName</key>       <string>Yolish Source File</string>
      <key>CFBundleTypeIconFile</key>   <string>file.png</string>
      <key>CFBundleTypeRole</key>       <string>Editor</string>
    </dict>
  </array>
</dict>
</plist>
PLIST

    # Register with LaunchServices
    if command -v lsregister >/dev/null 2>&1; then
        lsregister -f "$HOME/Library/Application Support/Yolish/Yolish.app" 2>/dev/null || true
    fi
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
        -f "$HOME/Library/Application Support/Yolish/Yolish.app" 2>/dev/null || true
    echo "      .y files registered (macOS LaunchServices)"
fi

#  Cleanup 
rm -f /tmp/yolish_logo.svg /tmp/yolish_file.png

#  Done 
echo ""
echo "=============================="
echo "  Yolish installed at $DEST"
echo ""
echo "  Run: ys hello.y"
echo "       ys -c hello.y"
echo "       ys              (REPL)"
echo ""
echo "  .y files now show Yolish icon"
echo "  (log out and back in if icons"
echo "   don't appear immediately)"
echo "=============================="
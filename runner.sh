set -e
CACHE_DIR="${TMPDIR:-/data/data/com.termux/files/usr/tmp}"
mkdir -p "$CACHE_DIR"
BIN="$CACHE_DIR/audio_patcher"
ARCH="$(uname -m)"
case "$ARCH" in
    aarch64|arm64)
        URL="https://raw.githubusercontent.com/merbah3266/CallReRecord-mute-setup/main/binaries/audio_patcher_arm64"
        ;;
    armv7l|armv8l|armv7|arm)
        URL="https://raw.githubusercontent.com/merbah3266/CallReRecord-mute-setup/main/binaries/audio_patcher_armv7"
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac
echo "Detected architecture: $ARCH"
echo "Downloading binary..."
if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "$BIN" "$URL"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$BIN" "$URL"
else
    echo "curl or wget is required"
    exit 1
fi
chmod 755 "$BIN"
echo "Checking root..."
if command -v su >/dev/null 2>&1 && su -c true >/dev/null 2>&1; then
    echo "Running with su..."
    su -c "$BIN"
elif command -v sudo >/dev/null 2>&1; then
    echo "Running with sudo..."
    sudo "$BIN"
else
    echo "Root access not found"
    exit 1
fi

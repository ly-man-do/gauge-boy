#!/bin/sh
# Download the pinned .cia packaging tools into tools/bin/ (gitignored):
#   makerom    (3DSGuy/Project_CTR)  builds the .cia from the .elf
#   bannertool (Epicpkmn11/bannertool) builds the HOME menu banner and icon
#
# Linux x86_64 only: run it inside the devkitPro Docker image, as `make cia`
# and CI do. Downloads are checked against pinned SHA-256 hashes.
#
# makerom is pinned to v0.18.4 because v0.19.0's Linux build needs glibc
# 2.38, newer than the devkitPro image's Debian 12.
set -eu

MAKEROM_URL="https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip"
MAKEROM_SHA256="dd596854718c195c6e3229286be485b122921715555af8ae5cf8e9a465d9f970"
BANNERTOOL_URL="https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip"
BANNERTOOL_SHA256="e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed"

cd "$(dirname "$0")"
BIN="$PWD/bin"
STAMP="$BIN/.versions"
WANT="$MAKEROM_SHA256 $BANNERTOOL_SHA256"

if [ -x "$BIN/makerom" ] && [ -x "$BIN/bannertool" ] && [ -f "$STAMP" ] \
    && [ "$(cat "$STAMP")" = "$WANT" ]; then
    exit 0
fi

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    echo "get-cia-tools.sh: needs Linux x86_64. Run it in the devkitPro image:" >&2
    echo "  docker run --rm --platform linux/amd64 -v \"\$PWD\":/src -w /src devkitpro/devkitarm make cia" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch() { # url sha256 file
    echo "Downloading $1"
    curl -fsSL --retry 3 -o "$TMP/$3" "$1"
    echo "$2  $TMP/$3" | sha256sum -c - >/dev/null || {
        echo "get-cia-tools.sh: SHA-256 mismatch for $1" >&2
        exit 1
    }
}

fetch "$MAKEROM_URL" "$MAKEROM_SHA256" makerom.zip
fetch "$BANNERTOOL_URL" "$BANNERTOOL_SHA256" bannertool.zip

mkdir -p "$TMP/makerom" "$TMP/bannertool" "$BIN"
unzip -q "$TMP/makerom.zip" makerom -d "$TMP/makerom"
unzip -q "$TMP/bannertool.zip" linux-x86_64/bannertool -d "$TMP/bannertool"
install -m 755 "$TMP/makerom/makerom" "$BIN/makerom"
install -m 755 "$TMP/bannertool/linux-x86_64/bannertool" "$BIN/bannertool"
echo "$WANT" > "$STAMP"
echo "Installed makerom and bannertool in tools/bin/"

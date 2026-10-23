#!/bin/sh
# Baixa a amalgamação do SQLite para common/sqlite/ (dependência de build;
# não há pacote SQLite nas portlibs do devkitPro).
set -e
VER=3500400
YEAR=2025
DIR="$(cd "$(dirname "$0")/.." && pwd)/common/sqlite"
TMP="$(mktemp -d)"
curl -fsSL -o "$TMP/sqlite.zip" "https://www.sqlite.org/$YEAR/sqlite-amalgamation-$VER.zip"
unzip -q "$TMP/sqlite.zip" -d "$TMP"
cp "$TMP"/sqlite-amalgamation-$VER/sqlite3.c "$TMP"/sqlite-amalgamation-$VER/sqlite3.h "$DIR/"
rm -rf "$TMP"
echo "SQLite copiado para $DIR"

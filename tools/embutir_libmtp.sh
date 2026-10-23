#!/bin/sh
# PHONE DUMP > ANDROID USB: copia a libmatriz_mtp (libmtp LGPL compilada pelo CMake) e a libusb (LGPL), ligadas
# dinamicamente — como no Android File Transfer —, para Contents/Frameworks e aponta a libmtp para a cópia da libusb.
# Roda no POST_BUILD do alvo matriz.
#   $1 = bundle .app    $2 = libmatriz_mtp.dylib gerada pelo CMake (install name já em @executable_path/../Frameworks)
set -e
APP="$1"
LIBMTP="$2"
FW="$APP/Contents/Frameworks"
mkdir -p "$FW"
rm -f "$FW"/libmtp.[0-9]*.dylib   # cópia antiga da libmtp do Homebrew

NOME_MTP=$(basename "$LIBMTP")
cp -fL "$LIBMTP" "$FW/$NOME_MTP"
chmod u+w "$FW/$NOME_MTP"

USB=$(otool -L "$FW/$NOME_MTP" | awk '/libusb/ {print $1; exit}')
if [ -n "$USB" ] && [ "${USB#@}" = "$USB" ]; then
    NOME_USB=$(basename "$USB")
    cp -fL "$USB" "$FW/$NOME_USB"
    chmod u+w "$FW/$NOME_USB"
    install_name_tool -id "@executable_path/../Frameworks/$NOME_USB" "$FW/$NOME_USB"
    install_name_tool -change "$USB" "@executable_path/../Frameworks/$NOME_USB" "$FW/$NOME_MTP"
    codesign --force -s - "$FW/$NOME_USB" >/dev/null 2>&1
fi
codesign --force -s - "$FW/$NOME_MTP" >/dev/null 2>&1

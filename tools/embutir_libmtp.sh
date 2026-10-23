#!/bin/sh
# PHONE DUMP > ANDROID USB: copia a libmtp e a libusb (LGPL, ligadas dinamicamente — como no Android File Transfer)
# para Contents/Frameworks e aponta o executável e a libmtp para essas cópias. Roda no POST_BUILD do alvo matriz.
#   $1 = bundle .app    $2 = libmtp.dylib encontrada pelo CMake
set -e
APP="$1"
LIBMTP="$2"
FW="$APP/Contents/Frameworks"
mkdir -p "$FW"

ID_MTP=$(otool -D "$LIBMTP" | tail -1)
NOME_MTP=$(basename "$ID_MTP")
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
install_name_tool -id "@executable_path/../Frameworks/$NOME_MTP" "$FW/$NOME_MTP"
codesign --force -s - "$FW/$NOME_MTP" >/dev/null 2>&1

for EXE in "$APP/Contents/MacOS/"*; do
    if otool -L "$EXE" | grep -q "$ID_MTP"; then
        install_name_tool -change "$ID_MTP" "@executable_path/../Frameworks/$NOME_MTP" "$EXE"
        # install_name_tool invalida a assinatura ad-hoc do linker (obrigatória no Apple Silicon).
        codesign --force -s - "$EXE" >/dev/null 2>&1 || true
    fi
done

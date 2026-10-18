#!/bin/sh
# Package the Windows final build (tools/build_windows.cmd) like the AppImage:
#   "C:\Program Files\Git\bin\sh.exe" tools/make_windows_zip.sh
# Output: dist/ISS_DELUXEd-windows-x64.zip. Unzip anywhere writable (saves and
# mods live beside the exe; settings go to %APPDATA%\ISSDNative) and run
# "ISS DELUXEd.exe". No ROM is included.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDL="${SDL2_DIR:-$ROOT/../win-deps/SDL2-2.30.9}"
BUILD="$ROOT/build-win"
NAME=ISS_DELUXEd-windows-x64
OUT="$ROOT/dist/$NAME"
L="$OUT/licenses"

rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/mods" "$L"
cp "$BUILD/ISSDNative.exe" "$OUT/ISS DELUXEd.exe"
cp "$ROOT/recomp/aot_boot_deny.txt" "$SDL/lib/x64/SDL2.dll" "$OUT/"

# Only the mods the final build uses: the title HD tiles and the new logo.
cp -r "$ROOT/mods/title_screen_hd" "$ROOT/mods/title_logo" "$OUT/mods/"
echo "$(cat "$ROOT/VERSION")-$(git -C "$ROOT" rev-parse --short HEAD)" > "$OUT/version.txt"

cp "$ROOT/LICENSE" "$L/ISS-DELUXEd-GPL-3.0.txt"
cp "$ROOT/deps/snesrecomp/LICENSE" "$L/SNESRecomp-PolyForm-Noncommercial-1.0.0.txt"
cp "$ROOT/deps/snesrecomp/THIRD_PARTY_ATTRIBUTION.md" "$L/SNESRecomp-third-party.md"
cp "$ROOT/deps/snesrecomp/lib/recomp-net/LICENSE" "$L/recomp-net-MIT.txt"
cp "$ROOT/assets/launcher/DejaVu-LICENSE.txt" "$L/DejaVu-fonts.txt"
JUICE=$(find "$BUILD" -path '*juice*' -name LICENSE -print -quit)
[ -n "$JUICE" ] && cp "$JUICE" "$L/libjuice-MPL-2.0.txt"
cp "$SDL/COPYING.txt" "$L/SDL2-zlib.txt"
cat > "$L/README.txt" <<'TXT'
ISS DELUXE'd: a non-commercial community fork of ISSD Native.
Its own code is GPL-3.0. It includes SNESRecomp (PolyForm Noncommercial 1.0.0),
so this build may not be used commercially. stb_image / stb_truetype are public
domain. No ROM is included: supply your own International Superstar Soccer
Deluxe (USA) cartridge dump. International Superstar Soccer Deluxe and its marks
belong to their respective rights holders.
TXT

cd "$ROOT/dist"
powershell.exe -NoProfile -Command "Compress-Archive -Path '$NAME' -DestinationPath '$NAME.zip' -Force"
ls -la "$OUT.zip"

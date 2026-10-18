#!/bin/sh
# Build the ISS DELUXE'd AppImage from the portable final build:
#   tools/debian12.sh cmake -S /src -B /src/build-deb12 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#       -DCMAKE_C_COMPILER=clang-16 -DCMAKE_CXX_COMPILER=clang++-16 \
#       -DISSD_FINAL_BUILD=ON -DISSD_GIT_HASH=$(git rev-parse --short HEAD)
#   tools/debian12.sh cmake --build /src/build-deb12
#   tools/make_appimage.sh
# Output: dist/ISS_DELUXEd-x86_64.AppImage. Needs glibc >= 2.34 (Debian 12+,
# Ubuntu 22.04+, Fedora 35+). Bundles Debian 12's SDL2 and its helpers; relies
# on the host for glibc, GPU drivers, X11/Wayland/PulseAudio, D-Bus and the
# xdg-desktop-portal file dialog. No ROM is included.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEB="${DEBIAN12_ROOT:-$ROOT/../../iss2000/ext/debian12/root}"
TOOLS="${APPIMAGE_TOOLS:-$ROOT/../../iss2000/ext/appimage}"
BUILD="$ROOT/build-deb12"
APPDIR="$ROOT/dist/AppDir"
OUT="$ROOT/dist/ISS_DELUXEd-x86_64.AppImage"
LIBSRC="$DEB/usr/lib/x86_64-linux-gnu"
LIBS="libSDL2-2.0.so.0 libdecor-0.so.0 libsamplerate.so.0 libXss.so.1"
SHARE="$APPDIR/usr/share/iss-deluxed"
LICENSES="$APPDIR/usr/share/licenses/iss-deluxed"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$SHARE/mods" "$LICENSES" \
         "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD/ISSDNative" "$APPDIR/usr/bin/ISSDNative"
strip "$APPDIR/usr/bin/ISSDNative"
cp "$ROOT/recomp/aot_boot_deny.txt" "$APPDIR/usr/bin/"
for lib in $LIBS; do cp -L "$LIBSRC/$lib" "$APPDIR/usr/lib/$lib"; done

# Only the mods the final build uses: the title HD tiles and the new logo.
cp -r "$ROOT/mods/title_screen_hd" "$ROOT/mods/title_logo" "$SHARE/mods/"
VERSION="$(cat "$ROOT/VERSION")-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo build)"
echo "$VERSION" > "$SHARE/mods.stamp"

cp "$ROOT/LICENSE" "$LICENSES/ISS-DELUXEd-GPL-3.0.txt"
cp "$ROOT/deps/snesrecomp/LICENSE" "$LICENSES/SNESRecomp-PolyForm-Noncommercial-1.0.0.txt"
cp "$ROOT/deps/snesrecomp/THIRD_PARTY_ATTRIBUTION.md" "$LICENSES/SNESRecomp-third-party.md"
cp "$ROOT/deps/snesrecomp/lib/recomp-net/LICENSE" "$LICENSES/recomp-net-MIT.txt"
cp "$ROOT/assets/launcher/DejaVu-LICENSE.txt" "$LICENSES/DejaVu-fonts.txt"
JUICE=$(find "$BUILD" -path '*juice*' -name LICENSE -print -quit)
[ -n "$JUICE" ] && cp "$JUICE" "$LICENSES/libjuice-MPL-2.0.txt"
cp "$DEB/usr/share/doc/libsdl2-2.0-0/copyright" "$LICENSES/SDL2-copyright.txt" 2>/dev/null || true
cat > "$LICENSES/README.txt" <<'TXT'
ISS DELUXE'd: a non-commercial community fork of ISSD Native.
Its own code is GPL-3.0. It includes SNESRecomp (PolyForm Noncommercial 1.0.0),
so this build may not be used commercially. stb_image / stb_truetype are public
domain. No ROM is included: supply your own International Superstar Soccer
Deluxe (USA) cartridge dump. International Superstar Soccer Deluxe and its marks
belong to their respective rights holders.
TXT

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# Run from a writable folder: the game keeps saves and reads mods relative to
# it, and the AppImage itself is read-only. Bundled mods refresh on update.
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/iss-deluxed"
mkdir -p "$DATA"
if ! cmp -s "$HERE/usr/share/iss-deluxed/mods.stamp" "$DATA/mods.stamp" 2>/dev/null; then
    rm -rf "$DATA/mods"
    cp -r "$HERE/usr/share/iss-deluxed/mods" "$DATA/mods"
    chmod -R u+w "$DATA/mods"
    cp "$HERE/usr/share/iss-deluxed/mods.stamp" "$DATA/mods.stamp"
fi
cd "$DATA"
exec "$HERE/usr/bin/ISSDNative" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/iss-deluxed.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=ISS DELUXE'd
Comment=Widescreen remaster of International Superstar Soccer Deluxe (SNES), with online play
Exec=ISSDNative
Icon=iss-deluxed
Categories=Game;SportsGame;
Terminal=false
DESKTOP
cp "$APPDIR/iss-deluxed.desktop" "$APPDIR/usr/share/applications/"
cp "$ROOT/assets/launcher/icon.png" "$APPDIR/iss-deluxed.png"
cp "$ROOT/assets/launcher/icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/iss-deluxed.png"
ln -sf iss-deluxed.png "$APPDIR/.DirIcon"

mkdir -p "$ROOT/dist"
# Build to a temporary name and rename over the old file, so it works even
# while the old AppImage is running.
ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" --runtime-file "$TOOLS/runtime-x86_64" "$APPDIR" "$OUT.new"
mv -f "$OUT.new" "$OUT"
rm -rf "$APPDIR"
ls -la "$OUT"

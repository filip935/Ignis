#!/usr/bin/env bash
set -euo pipefail

BIN="nihilflash-gui"
APPDIR="AppDir"
APPNAME="NihilFlash"
OUTPUT="${APPNAME}-x86_64.AppImage"

if [ ! -f "$BIN" ]; then
    echo "Run 'make' first to build the binary"
    exit 1
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"

cp "$BIN" "$APPDIR/usr/bin/"

copy_deps() {
    local exe="$1"
    while IFS= read -r line; do
        case "$line" in
            *"=> /"*)
                lib=$(echo "$line" | sed 's/.*=> //;s/ (.*//')
                dest="$APPDIR/usr/lib/$(basename "$lib")"
                if [ ! -f "$dest" ] && [ -f "$lib" ]; then
                    cp -L "$lib" "$dest"
                    copy_deps "$dest"
                fi
                ;;
        esac
    done < <(ldd "$exe" 2>/dev/null || true)
}

copy_deps "$APPDIR/usr/bin/$BIN"

for ban in libc.so libm.so libdl.so librt.so libpthread.so libstdc++.so libgcc_s.so ld-linux; do
    rm -f "$APPDIR/usr/lib/$ban"*
done

for lib in "$APPDIR/usr/lib"/*.so*; do
    [ -f "$lib" ] || continue
    strip --strip-unneeded "$lib" 2>/dev/null || true
    patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
done
strip "$APPDIR/usr/bin/$BIN" 2>/dev/null || true
patchelf --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/$BIN"

if [ -d /usr/share/glib-2.0/schemas ]; then
    cp -r /usr/share/glib-2.0/schemas/*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
fi

if [ -d /usr/share/X11/xkb ]; then
    mkdir -p "$APPDIR/usr/share/X11"
    cp -r /usr/share/X11/xkb "$APPDIR/usr/share/X11/" 2>/dev/null || true
fi

cat > "$APPDIR/AppRun" <<'APPEND'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-}"
export GSETTINGS_SCHEMA_DIR="$HERE/usr/share/glib-2.0/schemas"
export XKB_CONFIG_ROOT="$HERE/usr/share/X11/xkb"
exec "$HERE/usr/bin/nihilflash-gui" "$@"
APPEND
chmod +x "$APPDIR/AppRun"

cp resources/com.nihilflash.desktop "$APPDIR/usr/share/applications/"
cp "$APPDIR/usr/share/applications/com.nihilflash.desktop" "$APPDIR/"
cp resources/nihilflash.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/nihilflash.png" "$APPDIR/"

if [ ! -f "appimagetool" ]; then
    echo "Downloading appimagetool..."
    wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -O appimagetool
    chmod +x appimagetool
fi

rm -f "$OUTPUT"
echo "Creating AppImage..."
ARCH=x86_64 ./appimagetool "$APPDIR" "$OUTPUT"
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"

#!/bin/sh
# Make the Noto TrueType fonts available to Mosaic as scalable X core
# fonts.  Builds ~/.mosaic/fonts with symlinks to the system Noto
# files and generates the fonts.scale/fonts.dir index; Mosaic appends
# that directory to the X server's font path at startup
# (mo_add_private_font_path in src/gui.c).
#
# Needs: mkfontscale and mkfontdir (xfonts-utils on Debian/Ubuntu),
# and the Noto fonts (fonts-noto-core).  Run once per user; rerun
# after Noto package updates.

set -e

FONTDIR="$HOME/.mosaic/fonts"

# where distributions put the Noto TTFs
CANDIDATES="
/usr/share/fonts/truetype/noto
/usr/share/fonts/noto
/usr/share/fonts/google-noto
/usr/local/share/fonts/noto
"

# No mono faces here: the X freetype backend gives every glyph of a
# fixed-width TTF the face's max advance, which makes text unreadably
# wide.  Mosaic uses the misc-fixed iso10646 bitmaps for mono slots.
WANTED="NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf \
NotoSerif-Regular.ttf NotoSerif-Bold.ttf NotoSerif-Italic.ttf"

command -v mkfontscale >/dev/null 2>&1 || {
    echo "error: mkfontscale not found (install xfonts-utils)" >&2
    exit 1
}

SRC=
for d in $CANDIDATES; do
    if [ -f "$d/NotoSans-Regular.ttf" ]; then
        SRC="$d"
        break
    fi
done
[ -n "$SRC" ] || {
    echo "error: Noto fonts not found (install fonts-noto-core)" >&2
    exit 1
}

mkdir -p "$FONTDIR"
linked=0
for f in $WANTED; do
    if [ -f "$SRC/$f" ]; then
        ln -sf "$SRC/$f" "$FONTDIR/$f"
        linked=$((linked + 1))
    fi
done
[ "$linked" -gt 0 ] || {
    echo "error: no usable Noto files in $SRC" >&2
    exit 1
}

cd "$FONTDIR"
mkfontscale .
mkfontdir .

echo "Installed $linked Noto fonts into $FONTDIR."
echo "Mosaic will pick them up on next start (Options -> Fonts -> Noto ...)."
echo "To use them in the current X session right away:"
echo "    xset +fp $FONTDIR; xset fp rehash"

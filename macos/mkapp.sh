#!/bin/sh
# Wraps build/papaya-mac into build/Papaya.app (no Xcode needed).
#
# The Homebrew libraries it links (mpg123, opusfile, faad2, libpng, ...)
# are copied into Contents/Frameworks and the binary pointed at them, so
# the app runs on Macs without Homebrew. The version comes from
# include/version.h, the icon is Papaya.icns (made by mkicon.py from
# meta/icon.png).
set -e
cd "$(dirname "$0")"
APP=build/Papaya.app
FW="$APP/Contents/Frameworks"

ver() { sed -n "s/^#define CTRMUS_VERSION_$1 //p" ../include/version.h; }
VERSION="$(ver MAJOR).$(ver MINOR).$(ver MICRO)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$FW"
cp build/papaya-mac "$APP/Contents/MacOS/Papaya"
cp Papaya.icns "$APP/Contents/Resources/Papaya.icns"
cat > "$APP/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Papaya</string>
<key>CFBundleDisplayName</key><string>Papaya</string>
<key>CFBundleIdentifier</key><string>com.wormprojects.papaya.mac</string>
<key>CFBundleExecutable</key><string>Papaya</string>
<key>CFBundleIconFile</key><string>Papaya</string>
<key>LSApplicationCategoryType</key><string>public.app-category.music</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>$VERSION</string>
<key>LSMinimumSystemVersion</key><string>14.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PL

# Homebrew (or other non-system) libraries a Mach-O file links.
deps() {
	otool -L "$1" | tail -n +2 | awk '{print $1}' |
		grep -v -e '^/System/' -e '^/usr/lib/' -e '^@' || true
}

# Copy one library (and, recursively, what it links) into Frameworks and
# point `file` at the copy.
bundle() {
	file=$1
	for lib in $(deps "$file"); do
		name=$(basename "$lib")
		if [ ! -f "$FW/$name" ]; then
			cp "$lib" "$FW/$name"
			chmod u+w "$FW/$name"
			install_name_tool -id "@rpath/$name" "$FW/$name" 2>/dev/null
			# A subshell: sh has no local variables, and the recursion
			# would overwrite this call's $file/$lib/$name.
			( bundle "$FW/$name" )
		fi
		install_name_tool -change "$lib" "@rpath/$name" "$file" 2>/dev/null
	done
}

bundle "$APP/Contents/MacOS/Papaya"
install_name_tool -add_rpath "@executable_path/../Frameworks" \
	"$APP/Contents/MacOS/Papaya" 2>/dev/null || true

# Ad-hoc sign (libraries first) so macOS lets it run.
for f in "$FW"/*.dylib; do
	[ -f "$f" ] && codesign --force --sign - "$f" >/dev/null 2>&1
done
codesign --force --sign - "$APP" >/dev/null 2>&1 || true
echo "built $APP ($VERSION)"

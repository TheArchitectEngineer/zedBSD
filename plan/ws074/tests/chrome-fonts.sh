#!/bin/sh
# ws074: writes the fontconfig file that makes the host's Chromium draw with the fonts
# browser uses (build/ws035-fonts: Inter for serif and sans-serif, JetBrains Mono for
# monospace, Droid Sans Fallback for the rest), into build/ws074-chrome/fonts.conf.
# chrome-shot.sh and chrome-boxes.py pass it to Chromium through FONTCONFIG_FILE.
#
#   sh plan/ws074/tests/chrome-fonts.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
fonts=$(readlink -f build/ws035-fonts)
mkdir -p build/ws074-chrome/cache
cat > build/ws074-chrome/fonts.conf <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$fonts</dir>
  <cachedir>$root/build/ws074-chrome/cache</cachedir>
  <alias binding="strong"><family>serif</family><prefer><family>Inter</family><family>Droid Sans Fallback</family></prefer></alias>
  <alias binding="strong"><family>sans-serif</family><prefer><family>Inter</family><family>Droid Sans Fallback</family></prefer></alias>
  <alias binding="strong"><family>monospace</family><prefer><family>JetBrains Mono</family><family>Droid Sans Fallback</family></prefer></alias>
  <match target="pattern"><edit name="family" mode="append"><string>Droid Sans Fallback</string></edit></match>
  <match target="font"><edit name="hinting" mode="assign"><bool>false</bool></edit><edit name="antialias" mode="assign"><bool>true</bool></edit></match>
</fontconfig>
EOF
echo build/ws074-chrome/fonts.conf

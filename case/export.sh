#!/bin/sh
# Writes every printable part of cabinet.scad to stl/ (needs OpenSCAD).
#   LAUNCH_BTN=24 sh export.sh   -> a 24 mm arcade launch button instead of 16 mm
cd "$(dirname "$0")"
OPENSCAD=${OPENSCAD:-openscad}
mkdir -p stl
for part in body deck backbox marquee; do
  echo "== $part"
  "$OPENSCAD" -o "stl/$part.stl" -D "PART=\"$part\"" -D "LAUNCH_BTN=${LAUNCH_BTN:-16}" cabinet.scad || exit 1
done

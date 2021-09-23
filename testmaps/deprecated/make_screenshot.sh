#!/bin/bash
set -v
set -x

LIGHT_EXE=~/dev/tyrutils/build-ninja/light/light
QUAKEDIR=~/quake
QUAKE_EXE=~/quake/QuakeSpasm-SDL2.app/Contents/MacOS/QuakeSpasm

imagename="$1"
mapname="$2"
params="$3"
viewpos="$4"
showlightmap="$5"

#strip off .map suffix
map_basename=$(basename $mapname .map)

$LIGHT_EXE -lit -extra4 $params $mapname

rm -fdr $QUAKEDIR/tyrutils-screenshots
mkdir $QUAKEDIR/tyrutils-screenshots
mkdir $QUAKEDIR/tyrutils-screenshots/maps

#copy over the map
cp $map_basename.{bsp,lit} $QUAKEDIR/tyrutils-screenshots/maps

#write an autoexec.cfg that will take the screenshot
AUTOEXEC="$QUAKEDIR/tyrutils-screenshots/autoexec.cfg"
cat << EOF > "$AUTOEXEC"
scr_conspeed 100000
scr_centertime 0
con_notifytime 0
map $map_basename
wait
wait
wait
wait
wait
EOF

if [ "$viewpos" ]; then
  cat << EOF >> "$AUTOEXEC"
setpos $viewpos
EOF
fi

showents=1
if [ $showlightmap -eq 1 ]; then
  showents=0
fi

cat << EOF >> "$AUTOEXEC"
fog 0
gl_texturemode gl_nearest
r_lightmap $showlightmap
r_drawviewmodel 0
r_drawentities $showents
viewsize 120
fov 110
gamma 1
wait
wait
wait
wait
screenshot
quit
EOF

#launch quake
$QUAKE_EXE -basedir $QUAKEDIR -nolauncher -window -width 1024 -height 768 -fsaa 8 -game tyrutils-screenshots

#convert the screenshot to jpg
convert $QUAKEDIR/tyrutils-screenshots/spasm0000.tga $imagename


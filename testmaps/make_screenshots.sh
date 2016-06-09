#!/bin/bash
set -v
set -x

LIGHT_EXE=~/dev/tyrutils/build-ninja/light/light
QUAKEDIR=~/quake
QUAKE_EXE=~/Library/Developer/Xcode/DerivedData/QuakeSpasm-alpgyufxkvrsawhefxaskvlihpyd/Build/Products/Debug/QuakeSpasm-SDL2.app/Contents/MacOS/QuakeSpasm-SDL2

make_screenshot () {
    imagename="$1"
    mapname="$2"
    params="$3"
    viewpos="$4"
    showlightmap="$5"

    #strip off .map suffix
    map_basename=$(basename $mapname .map)

    $LIGHT_EXE -lit $params $mapname

    rm -fdr $QUAKEDIR/tyrutils-screenshots
    mkdir $QUAKEDIR/tyrutils-screenshots
    mkdir $QUAKEDIR/tyrutils-screenshots/maps

    #copy over the map
    cp $map_basename.{bsp,lit} $QUAKEDIR/tyrutils-screenshots/maps

    #write an autoexec.cfg that will take the screenshot
    cat << EOF > $QUAKEDIR/tyrutils-screenshots/autoexec.cfg
scr_conspeed 100000
scr_centertime 0
con_notifytime 0
map $map_basename
wait
wait
wait
wait
wait
setpos $viewpos
fog 0
gamma 0.7
fov 110
r_lightmap $showlightmap
r_drawviewmodel 0
r_drawentities 0
viewsize 120
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
}

#dirt

AZAD_VIEWPOS="1043 -1704 2282 12 134 0"
AZAD_MAP="ad_azad.map"

DIRT_VIEWPOS="$AZAD_VIEWPOS"
DIRT_MAP="$AZAD_MAP"

# make_screenshot dirtdefault.jpg "$DIRT_MAP" "-dirt -dirtdebug" "$DIRT_VIEWPOS" 1
# make_screenshot dirtdepth_256.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtdepth 256" "$DIRT_VIEWPOS" 1
# make_screenshot dirtdepth_512.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtdepth 512" "$DIRT_VIEWPOS" 1
# make_screenshot dirtdepth_1024.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtdepth 1024" "$DIRT_VIEWPOS" 1

# make_screenshot dirtgain_0.75.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtgain 0.75" "$DIRT_VIEWPOS" 1
# make_screenshot dirtgain_0.5.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtgain 0.5" "$DIRT_VIEWPOS" 1

# make_screenshot dirtmode_1_dirtgain_0.5.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtgain 0.5 -dirtmode 1" "$DIRT_VIEWPOS" 1

# make_screenshot dirtscale_1.5.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtscale 1.5" "$DIRT_VIEWPOS" 1
# make_screenshot dirtscale_2.0.jpg "$DIRT_MAP" "-dirt -dirtdebug -dirtscale 2.0" "$DIRT_VIEWPOS" 1

#sunlight

SUN_POS="$AZAD_VIEWPOS"
SUN_MAP="$AZAD_MAP"

make_screenshot a_sunlight.jpg "$SUN_MAP" "-sunlight2 0 -sunlight3 0" "$SUN_POS" 1
make_screenshot a_sunlight2.jpg "$SUN_MAP" "-sunlight 0" "$SUN_POS" 1
make_screenshot a_sunlight_plus_sunlight2.jpg "$SUN_MAP" "" "$SUN_POS" 1

#phong

PHONG_POS="$AZAD_VIEWPOS"
PHONG_MAP="$AZAD_MAP"

make_screenshot phong_1_lightmap.jpg "$PHONG_MAP" "" "$PHONG_POS" 1
make_screenshot phong_0_lightmap.jpg "$PHONG_MAP" "-phong 0" "$PHONG_POS" 1
make_screenshot phong_1_normals.jpg "$PHONG_MAP" "-phongdebug" "$PHONG_POS" 1
make_screenshot phong_0_normals.jpg "$PHONG_MAP" "-phongdebug -phong 0" "$PHONG_POS" 1

# bounce

BOUNCE_POS="$AZAD_VIEWPOS"
BOUNCE_MAP="$AZAD_MAP"
BOUNCE_OPTS="-sunlight2 0 -sunlight3 0"

make_screenshot bouncescale0.0.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS" "$BOUNCE_POS" 1
make_screenshot bouncescale0.5.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncescale 0.5" "$BOUNCE_POS" 1
make_screenshot bouncescale1.0.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncescale 1" "$BOUNCE_POS" 1
make_screenshot bouncescale2.0.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncescale 2" "$BOUNCE_POS" 1

make_screenshot bouncecolorscale0.5.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncecolorscale 0.5" "$BOUNCE_POS" 1
make_screenshot bouncecolorscale1.0.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncecolorscale 1" "$BOUNCE_POS" 1

make_screenshot bouncescale0.0_debug.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncedebug -bouncescale 0" "$BOUNCE_POS" 1
make_screenshot bouncescale0.5_debug.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncedebug -bouncescale 0.5" "$BOUNCE_POS" 1
make_screenshot bouncescale1.0_debug.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncedebug -bouncescale 1" "$BOUNCE_POS" 1
make_screenshot bouncescale2.0_debug.jpg "$BOUNCE_MAP" "$BOUNCE_OPTS -bounce -bouncedebug -bouncescale 2" "$BOUNCE_POS" 1


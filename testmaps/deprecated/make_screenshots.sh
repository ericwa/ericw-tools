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

    $LIGHT_EXE -lit -extra4 $params $mapname

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
r_lightmap $showlightmap
r_drawviewmodel 0
r_drawentities 0
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
}

#dirt

DIRT_VIEWPOS="-1904 -871 847 4 38 0"

make_screenshot dirtdefault.jpg jam2_sock.map "-dirt -dirtdebug" "$DIRT_VIEWPOS" 1
make_screenshot dirtdepth_256.jpg jam2_sock.map "-dirt -dirtdebug -dirtdepth 256" "$DIRT_VIEWPOS" 1
make_screenshot dirtdepth_512.jpg jam2_sock.map "-dirt -dirtdebug -dirtdepth 512" "$DIRT_VIEWPOS" 1
make_screenshot dirtdepth_1024.jpg jam2_sock.map "-dirt -dirtdebug -dirtdepth 1024" "$DIRT_VIEWPOS" 1

make_screenshot dirtgain_0.75.jpg jam2_sock.map "-dirt -dirtdebug -dirtgain 0.75" "$DIRT_VIEWPOS" 1
make_screenshot dirtgain_0.5.jpg jam2_sock.map "-dirt -dirtdebug -dirtgain 0.5" "$DIRT_VIEWPOS" 1

make_screenshot dirtmode_1_dirtgain_0.5.jpg jam2_sock.map "-dirt -dirtdebug -dirtgain 0.5 -dirtmode 1" "$DIRT_VIEWPOS" 1

make_screenshot dirtscale_1.5.jpg jam2_sock.map "-dirt -dirtdebug -dirtscale 1.5" "$DIRT_VIEWPOS" 1
make_screenshot dirtscale_2.0.jpg jam2_sock.map "-dirt -dirtdebug -dirtscale 2.0" "$DIRT_VIEWPOS" 1

#sunlight

SUN_POS_A="$DIRT_VIEWPOS"
SUN_POS_B="-1851 499 1057 1 329 0"

#TODO: make light support -sunlight flags on command line so these can use the same map file

make_screenshot a_sunlight.jpg sunlight.map "" "$SUN_POS_A" 1
make_screenshot b_sunlight.jpg sunlight.map "" "$SUN_POS_B" 1
make_screenshot a_sunlight2.jpg sunlight2.map "" "$SUN_POS_A" 1
make_screenshot b_sunlight2.jpg sunlight2.map "" "$SUN_POS_B" 1
make_screenshot a_sunlight_plus_sunlight2.jpg sunlight_plus_sunlight2.map "" "$SUN_POS_A" 1
make_screenshot b_sunlight_plus_sunlight2.jpg sunlight_plus_sunlight2.map "" "$SUN_POS_B" 1

#phong

PHONG_POS="893 887 -252 7 293 0"
PHONG_MAP="ad_crucial.map"

make_screenshot phong_1_lightmap.jpg "$PHONG_MAP" "" "$PHONG_POS" 1
make_screenshot phong_0_lightmap.jpg "$PHONG_MAP" "-phong 0" "$PHONG_POS" 1
make_screenshot phong_1_normals.jpg "$PHONG_MAP" "-phongdebug" "$PHONG_POS" 1
make_screenshot phong_0_normals.jpg "$PHONG_MAP" "-phongdebug -phong 0" "$PHONG_POS" 1

# bounce

BOUNCE_POS="1043 -1704 2282 12 134 0"
BOUNCE_MAP="ad_azad.map"

make_screenshot bouncescale0.0.jpg "$BOUNCE_MAP" "" "$BOUNCE_POS" 1
make_screenshot bouncescale0.5.jpg "$BOUNCE_MAP" "-bounce -bouncescale 0.5" "$BOUNCE_POS" 1
make_screenshot bouncescale1.0.jpg "$BOUNCE_MAP" "-bounce -bouncescale 1" "$BOUNCE_POS" 1
make_screenshot bouncescale2.0.jpg "$BOUNCE_MAP" "-bounce -bouncescale 2" "$BOUNCE_POS" 1


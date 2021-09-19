#!/bin/bash

# usage:
# - qbsp, vis, light need to be in PATH before running
# - returns exit status 1 if any tests failed, otherwise 0

# print statements as they are executed
set -x

# checking for lack of crashes

qbsp -noverbose invalid_texture_axes.map || exit 1
light invalid_texture_axes.map || exit 1

# hash checks

# Compiles Quake E1M1 under all supported output formats,
# then compare the hashes with what's been committed to the repo.
# Then vis all of the bsp's, and check the hashes again.
# Then light them, and check the hashes again.
# 
# This check will naturally fail if any changes are made to the
# tools that alter the .bsp output - the idea is you would just
# regenerate the expected hashes, but check that the .bsp's still
# work in game at the same time.

HASH_CHECK_BSPS="e1m1-bsp29.bsp \
e1m1-bsp2.bsp \
e1m1-2psb.bsp \
e1m1-hexen2.bsp \
e1m1-hexen2-bsp2.bsp \
e1m1-hexen2-2psb.bsp"

qbsp -noverbose               quake_map_source/E1M1.map e1m1-bsp29.bsp       || exit 1
qbsp -noverbose         -bsp2 quake_map_source/E1M1.map e1m1-bsp2.bsp        || exit 1
qbsp -noverbose         -2psb quake_map_source/E1M1.map e1m1-2psb.bsp        || exit 1
qbsp -noverbose -hexen2       quake_map_source/E1M1.map e1m1-hexen2.bsp      || exit 1
qbsp -noverbose -hexen2 -bsp2 quake_map_source/E1M1.map e1m1-hexen2-bsp2.bsp || exit 1
qbsp -noverbose -hexen2 -2psb quake_map_source/E1M1.map e1m1-hexen2-2psb.bsp || exit 1

sha256sum ${HASH_CHECK_BSPS}

# now run vis

for bsp in ${HASH_CHECK_BSPS}; do
    vis -nostate ${bsp} || exit 1
done

# now run light

for bsp in ${HASH_CHECK_BSPS}; do
    light ${bsp} || exit 1
done

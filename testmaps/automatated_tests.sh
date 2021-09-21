#!/bin/bash

# usage:
#     ./automated_tests.sh [--update-hashes]
#
# If --update-hashes is given, updates the expected hash files.
# Otherwise tests the generated .bsp's match the expected hashes.
#
# qbsp, vis, light need to be in PATH before running.
#
# Returns exit status 1 if any tests failed, otherwise 0

# print statements as they are executed
set -x

UPDATE_HASHES=0
if [[ "$1" == "--update-hashes" ]]; then 
    UPDATE_HASHES=1
fi

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
e1m1-hexen2-2psb.bsp \
e1m1-hlbsp.bsp \
e1m1-bspxbrushes.bsp \
e1m1-bsp29-onlyents.bsp"

qbsp -noverbose               quake_map_source/E1M1.map e1m1-bsp29.bsp       || exit 1
qbsp -noverbose         -bsp2 quake_map_source/E1M1.map e1m1-bsp2.bsp        || exit 1
qbsp -noverbose         -2psb quake_map_source/E1M1.map e1m1-2psb.bsp        || exit 1
qbsp -noverbose -hexen2       quake_map_source/E1M1.map e1m1-hexen2.bsp      || exit 1
qbsp -noverbose -hexen2 -bsp2 quake_map_source/E1M1.map e1m1-hexen2-bsp2.bsp || exit 1
qbsp -noverbose -hexen2 -2psb quake_map_source/E1M1.map e1m1-hexen2-2psb.bsp || exit 1
qbsp -noverbose -hlbsp        quake_map_source/E1M1.map e1m1-hlbsp.bsp       || exit 1
qbsp -noverbose -wrbrushes    quake_map_source/E1M1.map e1m1-bspxbrushes.bsp || exit 1

# -onlyents test:
#  - start with a copy of e1m1-bsp29.bsp called e1m1-bsp29-onlyents.bsp
#  - make a E1M1-edited-ents.map by adding an extra entity
#  - patch e1m1-bsp29-onlyents.bsp with the entities lump from E1M1-edited-ents.map
cp e1m1-bsp29.bsp e1m1-bsp29-onlyents.bsp || exit 1
cp e1m1-bsp29.prt e1m1-bsp29-onlyents.prt || exit 1
cp quake_map_source/E1M1.map E1M1-edited-ents.map || exit 1
cat << EOF >> E1M1-edited-ents.map
{
"classname"	"weapon_nailgun"
"origin"	"112 2352 20"
"spawnflags"	"2048"
}
EOF
qbsp -onlyents E1M1-edited-ents.map e1m1-bsp29-onlyents.bsp || exit 1
rm E1M1-edited-ents.map || exit 1

if [[ $UPDATE_HASHES -ne 0 ]]; then
    sha256sum ${HASH_CHECK_BSPS} > qbsp.sha256sum || exit 1
else
    sha256sum --strict --check qbsp.sha256sum || exit 1
fi

# now run vis
# FIXME: vis output is nondeterministic when run with multiple threads!

for bsp in ${HASH_CHECK_BSPS}; do
    vis -nostate -threads 1 ${bsp} || exit 1
done

if [[ $UPDATE_HASHES -ne 0 ]]; then
    sha256sum ${HASH_CHECK_BSPS} > qbsp-vis.sha256sum || exit 1
else
    sha256sum --strict --check qbsp-vis.sha256sum || exit 1
fi

# FIXME: light output is nondeterministic so we can't check the hashes currently

for bsp in ${HASH_CHECK_BSPS}; do
    light -threads 1 ${bsp} || exit 1
done

# if [[ $UPDATE_HASHES -ne 0 ]]; then
#     sha256sum ${HASH_CHECK_BSPS} > qbsp-vis-light.sha256sum || exit 1
# else
#     sha256sum --strict --check qbsp-vis-light.sha256sum || exit 1
# fi

# leak tests on all id1 maps
cd quake_map_source
./leaktest.sh || exit 1

exit 0

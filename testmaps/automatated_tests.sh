#!/bin/bash

# usage:
#     ./automated_tests.sh [--update-hashes|--continue-on-failure]
#
# If --update-hashes is given, updates the expected hash files.
# Otherwise tests the generated .bsp's match the expected hashes.
#
# qbsp, vis, light, bspinfo need to be in PATH before running.
#
# Returns exit status 1 if any tests failed, otherwise 0

# print statements as they are executed
set -x

UPDATE_HASHES=0
CONTINUE_ON_FAILURE=0
if [[ "$1" == "--update-hashes" ]]; then 
    UPDATE_HASHES=1
elif [[ "$1" == "--continue-on-failure" ]]; then
    CONTINUE_ON_FAILURE=1
elif [[ "$1" != "" ]]; then
    echo "usage: ./automated_tests.sh [--update-hashes]"
    exit 1
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

# qbsp_brush_clipping_order.bsp omitted because it is not sealed, so doesn't have a .prt
HASH_CHECK_BSPS="qbsp_func_detail.bsp \
qbsp_func_detail_illusionary_plus_water.bsp \
qbsp_origin.bsp \
qbsp_angled_brush.bsp \
qbsp_sealing_point_entity_on_outside.bsp \
qbsp_tjunc_many_sided_face.bsp \
e1m1-bsp29.bsp \
e1m1-bsp2.bsp \
e1m1-2psb.bsp \
e1m1-hexen2.bsp \
e1m1-hexen2-bsp2.bsp \
e1m1-hexen2-2psb.bsp \
e1m1-hlbsp.bsp \
e1m1-bspxbrushes.bsp \
e1m1-bsp29-onlyents.bsp \
qbspfeatures.bsp"

HASH_CHECK_PRTS=${HASH_CHECK_BSPS//.bsp/.prt}

# for tiny test maps, we'll commit the .json export of the .bsp's
# directly to the git repo, so we can print a diff
COMMIT_JSON_MAPS="qbsp_func_detail.bsp \
qbsp_func_detail_illusionary_plus_water.bsp \
qbsp_origin.bsp \
qbsp_angled_brush.bsp \
qbsp_sealing_point_entity_on_outside.bsp \
qbsp_brush_clipping_order.bsp \
qbsp_tjunc_many_sided_face.bsp"

# smaller test maps for specific features/combinations
# check .json diff of COMMIT_JSON_MAPS
for bsp in ${COMMIT_JSON_MAPS}; do
    # save regular verbosity log to file, to avoid spamming the CI log
    qbsp -nopercent ${bsp} &> ${bsp}.qbsplog

    # dump .bsp to .bsp.json
    bspinfo ${bsp} || exit 1

    if [[ $UPDATE_HASHES -ne 0 ]]; then
        mkdir reference_bsp_json
        cp ${bsp}.json reference_bsp_json/${bsp}.json
        cp ${bsp}.qbsplog reference_bsp_json/${bsp}.qbsplog
    else
        echo "Diff of reference_bsp_json/${bsp}.qbsplog and ${bsp}.qbsplog:"
        diff -U3 -w reference_bsp_json/${bsp}.qbsplog ${bsp}.qbsplog

        echo "Diff of reference_bsp_json/${bsp}.json and ${bsp}.json:"
        diff -U3 -w reference_bsp_json/${bsp}.json ${bsp}.json

        diffreturn=$?
        if [[ $diffreturn -ne 0 ]]; then
            echo "Diff returned $diffreturn"
            file reference_bsp_json/${bsp}.json
            file ${bsp}.json

            if [[ $CONTINUE_ON_FAILURE -ne 1 ]]; then
                exit 1
            fi
        fi
    fi
done

# larger test maps (E1M1)
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
qbsp -onlyents E1M1-edited-ents.map e1m1-bsp29-onlyents.bsp || exit 1

qbsp -noverbose               qbspfeatures.map                               || exit 1

# check (or regenerate) hashes of .bsp's
if [[ $UPDATE_HASHES -ne 0 ]]; then
    sha256sum ${HASH_CHECK_BSPS} ${HASH_CHECK_PRTS} > qbsp.sha256sum || exit 1
else
    sha256sum --strict --check qbsp.sha256sum

    hash_check_return=$?
    if [[ $hash_check_return -ne 0 ]] && [[ $CONTINUE_ON_FAILURE -ne 1 ]]; then
        exit 1
    fi
fi

# now run vis
# FIXME: vis output is nondeterministic when run with multiple threads, so force 1 thread per process

for bsp in ${HASH_CHECK_BSPS}; do
    vis -nostate -threads 1 ${bsp} || exit 1
done

if [[ $UPDATE_HASHES -ne 0 ]]; then
    sha256sum ${HASH_CHECK_BSPS} > qbsp-vis.sha256sum || exit 1
else
    sha256sum --strict --check qbsp-vis.sha256sum

    hash_check_return=$?
    if [[ $hash_check_return -ne 0 ]] && [[ $CONTINUE_ON_FAILURE -ne 1 ]]; then
        exit 1
    fi
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

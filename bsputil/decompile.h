#pragma once

#include <cstdio>

struct mbsp_t;
struct bsp2_dnode_t;

struct decomp_options {
    /**
     * If true, use a simplified algorithm that just dumps the planes bounding each leaf,
     * without attempting to reconstruct faces or discard redundant planes.
     *
     * For debugging (there's not much that can go wrong).
     */
    bool geometryOnly = false;
};

void DecompileBSP(const mbsp_t *bsp, const decomp_options& options, FILE* file);

#pragma once

#include <fstream>

struct mbsp_t;
struct bsp2_dnode_t;

struct decomp_options
{
    /**
     * If true, use a simplified algorithm that just dumps the planes bounding each leaf,
     * without attempting to reconstruct faces or discard redundant planes.
     *
     * For debugging (there's not much that can go wrong).
     */
    bool geometryOnly = false;
    /**
     * If true, don't use brushes in Q2 .bsp's and instead decompile the leafs.
     * Intended for visualizing leafs.
     */
    bool ignoreBrushes = false;

    int hullnum = 0;
};

void DecompileBSP(const mbsp_t *bsp, const decomp_options &options, std::ofstream &file);

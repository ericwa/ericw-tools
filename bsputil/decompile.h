#pragma once

#include <cstdio>

struct mbsp_t;
struct bsp2_dnode_t;

void DecompileBSP(const mbsp_t *bsp, FILE* file);

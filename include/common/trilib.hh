/* common/trilib.h
 *
 * Header file for loading triangles from an Alias triangle file
 *
 */

#ifndef __COMMON_TRILIB_H__
#define __COMMON_TRILIB_H__

#define MAXTRIANGLES 2048

typedef struct {
    vec3_t verts[3];
} triangle_t;

void LoadTriangleList(char *filename, triangle_t ** pptri, int *numtriangles);

#endif /* __COMMON_TRILIB_H__ */

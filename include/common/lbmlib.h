/* common/lbmlib.h */

#ifndef __COMMON_LBMLIB_H__
#define __COMMON_LBMLIB_H__

typedef unsigned char UBYTE;
typedef short WORD;
typedef unsigned short UWORD;
typedef long LONG;

typedef enum {
    ms_none,
    ms_mask,
    ms_transcolor,
    ms_lasso
} mask_t;

typedef enum {
    cm_none,
    cm_rle1
} compress_t;

typedef struct {
    UWORD w, h;
    WORD x, y;
    UBYTE nPlanes;
    UBYTE masking;
    UBYTE compression;
    UBYTE pad1;
    UWORD transparentColor;
    UBYTE xAspect, yAspect;
    WORD pageWidth, pageHeight;
} bmhd_t;

extern bmhd_t bmhd;		/* will be in native byte order */

void LoadLBM(char *filename, byte **picture, byte **palette);

void WriteLBMfile(char *filename, byte *data, int width, int height,
		  byte *palette);

#endif /* __COMMON_LBMLIB_H__ */

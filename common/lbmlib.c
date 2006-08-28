/* common/lbmlib.c */

#include <common/cmdlib.h>
#include <common/lbmlib.h>

/*
 * ============================================================================
 *                                  LBM STUFF
 * ============================================================================
 */

#define FORMID ('F'+('O'<<8)+((int)'R'<<16)+((int)'M'<<24))
#define ILBMID ('I'+('L'<<8)+((int)'B'<<16)+((int)'M'<<24))
#define PBMID  ('P'+('B'<<8)+((int)'M'<<16)+((int)' '<<24))
#define BMHDID ('B'+('M'<<8)+((int)'H'<<16)+((int)'D'<<24))
#define BODYID ('B'+('O'<<8)+((int)'D'<<16)+((int)'Y'<<24))
#define CMAPID ('C'+('M'<<8)+((int)'A'<<16)+((int)'P'<<24))

bmhd_t bmhd;

int
Align(int l)
{
    if (l & 1)
	return l + 1;
    return l;
}

/*
 * ================
 * LBMRLEdecompress
 * Source must be evenly aligned!
 * ================
 */
byte *
LBMRLEDecompress(byte *source, byte *unpacked, int bpwidth)
{
    int count;
    byte b, rept;

    count = 0;

    do {
	rept = *source++;

	if (rept > 0x80) {
	    rept = (rept ^ 0xff) + 2;
	    b = *source++;
	    memset(unpacked, b, rept);
	    unpacked += rept;
	} else if (rept < 0x80) {
	    rept++;
	    memcpy(unpacked, source, rept);
	    unpacked += rept;
	    source += rept;
	} else
	    rept = 0;		/* rept of 0x80 is NOP */

	count += rept;

    } while (count < bpwidth);

    if (count > bpwidth)
	Error("Decompression exceeded width!\n");

    return source;
}

#define BPLANESIZE 128
byte bitplanes[9][BPLANESIZE];	/* max size 1024 by 9 bit planes */

/*
 * =================
 * MungeBitPlanes8
 * This destroys the bit plane data!
 * =================
 */
void
MungeBitPlanes8(int width, byte *dest)
{
    *dest = width;		/* shut up the compiler warning */
    Error("MungeBitPlanes8 not rewritten!");
#if 0
    asm les di,[dest]
    asm mov si, -1 asm mov cx,[width]






     
	
	
	mungebyte:asm inc si
	asm mov dx, 8
	mungebit:asm shl[BYTE PTR bitplanes + BPLANESIZE * 7 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 6 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 5 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 4 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 3 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 2 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 1 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 0 + si], 1
	asm rcl al, 1
	asm stosb
	asm dec cx
	asm jz done asm dec dx asm jnz mungebit asm jmp mungebyte done:
#endif
} void
MungeBitPlanes4(int width, byte *dest)
{
    *dest = width;		/* shut up the compiler warning */
    Error("MungeBitPlanes4 not rewritten!");
#if 0

    asm les di,[dest]
    asm mov si, -1 asm mov cx,[width]






     
	
	
	mungebyte:asm inc si
	asm mov dx, 8
	mungebit:asm xor al, al
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 3 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 2 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 1 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 0 + si], 1
	asm rcl al, 1
	asm stosb
	asm dec cx
	asm jz done asm dec dx asm jnz mungebit asm jmp mungebyte done:
#endif
} void
MungeBitPlanes2(int width, byte *dest)
{
    *dest = width;		/* shut up the compiler warning */
    Error("MungeBitPlanes2 not rewritten!");
#if 0
    asm les di,[dest]
    asm mov si, -1 asm mov cx,[width]






     
	
	
	mungebyte:asm inc si
	asm mov dx, 8
	mungebit:asm xor al, al
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 1 + si], 1
	asm rcl al, 1
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 0 + si], 1
	asm rcl al, 1
	asm stosb
	asm dec cx
	asm jz done asm dec dx asm jnz mungebit asm jmp mungebyte done:
#endif
} void
MungeBitPlanes1(int width, byte *dest)
{
    *dest = width;		/* shut up the compiler warning */
    Error("MungeBitPlanes1 not rewritten!");
#if 0
    asm les di,[dest]
    asm mov si, -1 asm mov cx,[width]






     
	
	
	mungebyte:asm inc si
	asm mov dx, 8
	mungebit:asm xor al, al
	asm shl[BYTE PTR bitplanes + BPLANESIZE * 0 + si], 1
	asm rcl al, 1
	asm stosb
	asm dec cx
	asm jz done asm dec dx asm jnz mungebit asm jmp mungebyte done:
#endif
}
/*
 * =================
 * LoadLBM
 * =================
 */ void
LoadLBM(char *filename, byte **picture, byte **palette)
{
    byte *LBMbuffer, *picbuffer, *cmapbuffer;
    int y, p, planes;
    byte *LBM_P, *LBMEND_P;
    byte *pic_p;
    byte *body_p;
    unsigned rowsize;

    int formtype, formlength;
    int chunktype, chunklength;
    void (*mungecall) (int, byte *);

    /* quiet compiler warnings */
    picbuffer = NULL;
    cmapbuffer = NULL;
    mungecall = NULL;

    /* load the LBM */
    LoadFile(filename, (void **)&LBMbuffer);

    /* parse the LBM header */
    LBM_P = LBMbuffer;
    if (*(int *)LBMbuffer != LittleLong(FORMID))
	Error("No FORM ID at start of file!\n");

    LBM_P += 4;
    formlength = BigLong(*(int *)LBM_P);
    LBM_P += 4;
    LBMEND_P = LBM_P + Align(formlength);

    formtype = LittleLong(*(int *)LBM_P);

    if (formtype != ILBMID && formtype != PBMID)
	Error("Unrecognized form type: %c%c%c%c\n", formtype & 0xff,
	      (formtype >> 8) & 0xff, (formtype >> 16) & 0xff,
	      (formtype >> 24) & 0xff);
    LBM_P += 4;

    /* parse chunks */
    while (LBM_P < LBMEND_P) {
	chunktype =
	    LBM_P[0] + (LBM_P[1] << 8) + (LBM_P[2] << 16) + (LBM_P[3] << 24);
	LBM_P += 4;
	chunklength =
	    LBM_P[3] + (LBM_P[2] << 8) + (LBM_P[1] << 16) + (LBM_P[0] << 24);
	LBM_P += 4;

	switch (chunktype) {
	case BMHDID:
	    memcpy(&bmhd, LBM_P, sizeof(bmhd));
	    bmhd.w = BigShort(bmhd.w);
	    bmhd.h = BigShort(bmhd.h);
	    bmhd.x = BigShort(bmhd.x);
	    bmhd.y = BigShort(bmhd.y);
	    bmhd.pageWidth = BigShort(bmhd.pageWidth);
	    bmhd.pageHeight = BigShort(bmhd.pageHeight);
	    break;

	case CMAPID:
	    cmapbuffer = malloc(768);
	    memset(cmapbuffer, 0, 768);
	    memcpy(cmapbuffer, LBM_P, chunklength);
	    break;

	case BODYID:
	    body_p = LBM_P;

	    pic_p = picbuffer = malloc(bmhd.w * bmhd.h);
	    if (formtype == PBMID) {

		/* unpack PBM */
		for (y = 0; y < bmhd.h; y++, pic_p += bmhd.w) {
		    if (bmhd.compression == cm_rle1) {
			body_p = LBMRLEDecompress((byte *)body_p,
						  pic_p, bmhd.w);
		    } else if (bmhd.compression == cm_none) {
			memcpy(pic_p, body_p, bmhd.w);
			body_p += Align(bmhd.w);
		    }
		}

	    } else {

		/* unpack ILBM */
		planes = bmhd.nPlanes;
		if (bmhd.masking == ms_mask)
		    planes++;
		rowsize = (bmhd.w + 15) / 16 * 2;
		switch (bmhd.nPlanes) {
		case 1:
		    mungecall = MungeBitPlanes1;
		    break;
		case 2:
		    mungecall = MungeBitPlanes2;
		    break;
		case 4:
		    mungecall = MungeBitPlanes4;
		    break;
		case 8:
		    mungecall = MungeBitPlanes8;
		    break;
		default:
		    Error("Can't munge %i bit planes!\n", bmhd.nPlanes);
		}

		for (y = 0; y < bmhd.h; y++, pic_p += bmhd.w) {
		    for (p = 0; p < planes; p++)
			if (bmhd.compression == cm_rle1) {
			    body_p = LBMRLEDecompress((byte *)body_p,
						      bitplanes[p], rowsize);
			} else if (bmhd.compression == cm_none) {
			    memcpy(bitplanes[p], body_p, rowsize);
			    body_p += rowsize;
			}

		    mungecall(bmhd.w, pic_p);
		}
	    }
	    break;
	}

	LBM_P += Align(chunklength);
    }

    free(LBMbuffer);

    *picture = picbuffer;
    *palette = cmapbuffer;
}


/*
 * ============================================================================
 *                                  WRITE LBM
 * ============================================================================
 */


/*
 * ==============
 * WriteLBMfile
 * ==============
 */
void
WriteLBMfile(char *filename, byte *data, int width, int height, byte *palette)
{
    byte *lbm, *lbmptr;
    int *formlength, *bmhdlength, *cmaplength, *bodylength;
    int length;
    bmhd_t basebmhd;

    lbm = lbmptr = malloc(width * height + 1000);


    /* start FORM */
    *lbmptr++ = 'F';
    *lbmptr++ = 'O';
    *lbmptr++ = 'R';
    *lbmptr++ = 'M';

    formlength = (int *)lbmptr;
    lbmptr += 4;		/* leave space for length */

    *lbmptr++ = 'P';
    *lbmptr++ = 'B';
    *lbmptr++ = 'M';
    *lbmptr++ = ' ';

    /* write BMHD */
    *lbmptr++ = 'B';
    *lbmptr++ = 'M';
    *lbmptr++ = 'H';
    *lbmptr++ = 'D';

    bmhdlength = (int *)lbmptr;
    lbmptr += 4;		/* leave space for length */

    memset(&basebmhd, 0, sizeof(basebmhd));
    basebmhd.w = BigShort((short)width);
    basebmhd.h = BigShort((short)height);
    basebmhd.nPlanes = BigShort(8);
    basebmhd.xAspect = BigShort(5);
    basebmhd.yAspect = BigShort(6);
    basebmhd.pageWidth = BigShort((short)width);
    basebmhd.pageHeight = BigShort((short)height);

    memcpy(lbmptr, &basebmhd, sizeof(basebmhd));
    lbmptr += sizeof(basebmhd);

    length = lbmptr - (byte *)bmhdlength - 4;
    *bmhdlength = BigLong(length);
    if (length & 1)
	*lbmptr++ = 0;		/* pad chunk to even offset */

    /* write CMAP */
    *lbmptr++ = 'C';
    *lbmptr++ = 'M';
    *lbmptr++ = 'A';
    *lbmptr++ = 'P';

    cmaplength = (int *)lbmptr;
    lbmptr += 4;		/* leave space for length */

    memcpy(lbmptr, palette, 768);
    lbmptr += 768;

    length = lbmptr - (byte *)cmaplength - 4;
    *cmaplength = BigLong(length);
    if (length & 1)
	*lbmptr++ = 0;		/* pad chunk to even offset */

    /* write BODY */
    *lbmptr++ = 'B';
    *lbmptr++ = 'O';
    *lbmptr++ = 'D';
    *lbmptr++ = 'Y';

    bodylength = (int *)lbmptr;
    lbmptr += 4;		/* leave space for length */

    memcpy(lbmptr, data, width * height);
    lbmptr += width * height;

    length = lbmptr - (byte *)bodylength - 4;
    *bodylength = BigLong(length);
    if (length & 1)
	*lbmptr++ = 0;		/* pad chunk to even offset */

    /* done */
    length = lbmptr - (byte *)formlength - 4;
    *formlength = BigLong(length);
    if (length & 1)
	*lbmptr++ = 0;		/* pad chunk to even offset */

    /* write output file */
    SaveFile(filename, lbm, lbmptr - lbm);
    free(lbm);
}

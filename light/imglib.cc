#include <light/imglib.hh>
#include <light/entities.hh>
#include <map>
#include <vector>


/*
============================================================================
PALETTE
============================================================================
*/

uint8_t thepalette[768] = // Quake palette
{
    0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
    139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
    55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
    171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
    51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
    43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};

uint8_t hexen2palette[768] = //mxd
{
    0,0,0,0,0,0,8,8,8,16,16,16,24,24,24,32,32,32,40,40,40,48,48,48,56,56,56,64,64,64,72,72,72,80,80,80,84,84,84,88,88,88,96,96,96,104,104,104,112,112,112,120,120,120,128,128,128,136,136,136,148,148,148,156,156,156,168,168,168,180,180,180,184,184,184,196,196,196,204,204,204,212,212,212,224,224,224,232,232,232,240,240,240,252,252,252,8,8,12,16,16,20,24,24,28,28,32,36,36,36,44,44,44,52,48,52,60,56,56,68,64,64,72,76,
    76,88,92,92,104,108,112,128,128,132,152,152,156,176,168,172,196,188,196,220,32,24,20,40,32,28,48,36,32,52,44,40,60,52,44,68,56,52,76,64,56,84,72,64,92,76,72,100,84,76,108,92,84,112,96,88,120,104,96,128,112,100,136,116,108,144,124,112,20,24,20,28,32,28,32,36,32,40,44,40,44,48,44,48,56,48,56,64,56,64,68,64,68,76,68,84,92,84,104,112,104,120,128,120,140,148,136,156,164,152,172,180,168,188,196,184,48,32,8,60,40,8,
    72,48,16,84,56,20,92,64,28,100,72,36,108,80,44,120,92,52,136,104,60,148,116,72,160,128,84,168,136,92,180,144,100,188,152,108,196,160,116,204,168,124,16,20,16,20,28,20,24,32,24,28,36,28,32,44,32,36,48,36,40,56,40,44,60,44,48,68,48,52,76,52,60,84,60,68,92,64,76,100,72,84,108,76,92,116,84,100,128,92,24,12,8,32,16,8,40,20,8,52,24,12,60,28,12,68,32,12,76,36,16,84,44,20,92,48,24,100,56,28,112,64,32,120,72,36,128,80,
    44,144,92,56,168,112,72,192,132,88,24,4,4,36,4,4,48,0,0,60,0,0,68,0,0,80,0,0,88,0,0,100,0,0,112,0,0,132,0,0,152,0,0,172,0,0,192,0,0,212,0,0,232,0,0,252,0,0,16,12,32,28,20,48,32,28,56,40,36,68,52,44,80,60,56,92,68,64,104,80,72,116,88,84,128,100,96,140,108,108,152,120,116,164,132,132,176,144,144,188,156,156,200,172,172,212,36,20,4,52,24,4,68,32,4,80,40,0,100,48,4,124,60,4,140,72,4,156,88,8,172,100,8,188,116,12,
    204,128,12,220,144,16,236,160,20,252,184,56,248,200,80,248,220,120,20,16,4,28,24,8,36,32,8,44,40,12,52,48,16,56,56,16,64,64,20,68,72,24,72,80,28,80,92,32,84,104,40,88,116,44,92,128,52,92,140,52,92,148,56,96,160,64,60,16,16,72,24,24,84,28,28,100,36,36,112,44,44,124,52,48,140,64,56,152,76,64,44,20,8,56,28,12,72,32,16,84,40,20,96,44,28,112,52,32,124,56,40,140,64,48,24,20,16,36,28,20,44,36,28,56,44,32,64,52,36,72,
    60,44,80,68,48,92,76,52,100,84,60,112,92,68,120,100,72,132,112,80,144,120,88,152,128,96,160,136,104,168,148,112,36,24,12,44,32,16,52,40,20,60,44,20,72,52,24,80,60,28,88,68,28,104,76,32,148,96,56,160,108,64,172,116,72,180,124,80,192,132,88,204,140,92,216,156,108,60,20,92,100,36,116,168,72,164,204,108,192,4,84,4,4,132,4,0,180,0,0,216,0,4,4,144,16,68,204,36,132,224,88,168,232,216,4,4,244,72,0,252,128,0,252,172,24,252,252,252
};

uint8_t quake2palette[768] = //mxd
{
    0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,99,75,35,91,67,31,83,63,31,79,59,27,71,55,27,63,47,23,59,43,23,51,39,19,47,35,19,43,31,19,39,27,15,35,23,15,27,19,11,23,15,11,19,15,7,15,11,7,95,95,111,91,91,103,91,83,95,87,79,91,83,75,83,79,71,75,71,63,67,63,59,59,59,55,55,51,47,47,47,43,43,
    39,39,39,35,35,35,27,27,27,23,23,23,19,19,19,143,119,83,123,99,67,115,91,59,103,79,47,207,151,75,167,123,59,139,103,47,111,83,39,235,159,39,203,139,35,175,119,31,147,99,27,119,79,23,91,59,15,63,39,11,35,23,7,167,59,43,159,47,35,151,43,27,139,39,19,127,31,15,115,23,11,103,23,7,87,19,0,75,15,0,67,15,0,59,15,0,51,11,0,43,11,0,35,11,0,27,7,0,19,7,0,123,95,75,115,87,67,107,83,63,103,79,59,95,71,55,87,67,51,83,63,
    47,75,55,43,67,51,39,63,47,35,55,39,27,47,35,23,39,27,19,31,23,15,23,15,11,15,11,7,111,59,23,95,55,23,83,47,23,67,43,23,55,35,19,39,27,15,27,19,11,15,11,7,179,91,79,191,123,111,203,155,147,215,187,183,203,215,223,179,199,211,159,183,195,135,167,183,115,151,167,91,135,155,71,119,139,47,103,127,23,83,111,19,75,103,15,67,91,11,63,83,7,55,75,7,47,63,7,39,51,0,31,43,0,23,31,0,15,19,0,7,11,0,0,0,139,87,87,131,79,79,
    123,71,71,115,67,67,107,59,59,99,51,51,91,47,47,87,43,43,75,35,35,63,31,31,51,27,27,43,19,19,31,15,15,19,11,11,11,7,7,0,0,0,151,159,123,143,151,115,135,139,107,127,131,99,119,123,95,115,115,87,107,107,79,99,99,71,91,91,67,79,79,59,67,67,51,55,55,43,47,47,35,35,35,27,23,23,19,15,15,11,159,75,63,147,67,55,139,59,47,127,55,39,119,47,35,107,43,27,99,35,23,87,31,19,79,27,15,67,23,11,55,19,11,43,15,7,31,11,7,23,7,0,
    11,0,0,0,0,0,119,123,207,111,115,195,103,107,183,99,99,167,91,91,155,83,87,143,75,79,127,71,71,115,63,63,103,55,55,87,47,47,75,39,39,63,35,31,47,27,23,35,19,15,23,11,7,7,155,171,123,143,159,111,135,151,99,123,139,87,115,131,75,103,119,67,95,111,59,87,103,51,75,91,39,63,79,27,55,67,19,47,59,11,35,47,7,27,35,0,19,23,0,11,15,0,0,255,0,35,231,15,63,211,27,83,187,39,95,167,47,95,143,51,95,123,51,255,255,255,255,255,
    211,255,255,167,255,255,127,255,255,83,255,255,39,255,235,31,255,215,23,255,191,15,255,171,7,255,147,0,239,127,0,227,107,0,211,87,0,199,71,0,183,59,0,171,43,0,155,31,0,143,23,0,127,15,0,115,7,0,95,0,0,71,0,0,47,0,0,27,0,0,239,0,0,55,55,255,255,0,0,0,0,255,43,43,35,27,27,23,19,19,15,235,151,127,195,115,83,159,87,51,123,63,27,235,211,199,199,171,155,167,139,119,135,107,87,159,91,83
};

void // WHO TOUCHED MY PALET?
LoadPalette(bspdata_t *bspdata)
{
    // Load Quake 2 palette
    if (bspdata->loadversion == Q2_BSPVERSION) {
        uint8_t *palette;
        char path[1024];
        char colormap[] = "pics/colormap.pcx";

        sprintf(path, "%s%s", gamedir, colormap);
        if (FileTime(path) == -1 || !LoadPCX(path, nullptr, &palette, nullptr, nullptr)) {
            if (Q_strcasecmp(gamedir, basedir)) {
                sprintf(path, "%s%s", basedir, colormap);
                if (FileTime(path) == -1 || !LoadPCX(path, nullptr, &palette, nullptr, nullptr)) {
                    logprint("WARNING: failed to load palette from '%s%s' or '%s%s'.\nUsing built-in palette.\n", gamedir, colormap, basedir, colormap);
                    palette = quake2palette;
                }
            } else {
                logprint("WARNING: failed to load palette from '%s%s'.\nUsing built-in palette.\n", gamedir, colormap);
                palette = quake2palette;
            }
        }

        for (int i = 0; i < 768; i++)
            thepalette[i] = palette[i];

    } else if (bspdata->hullcount == MAX_MAP_HULLS_H2) { // Gross hacks
        // Copy Hexen 2 palette
        for (int i = 0; i < 768; i++)
            thepalette[i] = hexen2palette[i];
    }
}

qvec3f Palette_GetColor(const int i)
{
    return qvec3f((float)thepalette[3 * i],
                  (float)thepalette[3 * i + 1],
                  (float)thepalette[3 * i + 2]);
}

qvec4f Texture_GetColor(const rgba_miptex_t *tex, const int i)
{
    const uint8_t *data = (uint8_t*)tex + tex->offset;
    return qvec4f{ (float)data[i * 4], 
                   (float)data[i * 4 + 1], 
                   (float)data[i * 4 + 2],
                   (float)data[i * 4 + 3] };
}

/*
============================================================================
PCX IMAGE
============================================================================
*/

//mxd. Copied from https://github.com/qbism/q2tools-220/blob/f8f582fc02196955584542c95de8a45a138c9e42/common/lbmlib.c#L383
typedef struct
{
    char	manufacturer;
    char	version;
    char	encoding;
    char	bits_per_pixel;
    unsigned short	xmin, ymin, xmax, ymax;
    unsigned short	hres, vres;
    unsigned char	palette[48];
    char	reserved;
    char	color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    char	filler[58];
    unsigned char	data;			// unbounded
} pcx_t;


/*
==============
LoadPCX
==============
*/
qboolean
LoadPCX(const char *filename, uint8_t **pixels, uint8_t **palette, int *width, int *height)
{
    uint8_t	*raw;
    int runLength;

    // Load the file
    if (FileTime(filename) == -1) {
        logprint("LoadPCX: Failed to load '%s'. File does not exist.\n", filename);
        return false; //mxd. Because LoadFile will throw Error if the file doesn't exist... 
    }
    const int len = LoadFile(filename, reinterpret_cast<void **>(&raw));
    if (len < 1) {
        logprint("LoadPCX: Failed to load '%s'. File is empty.\n", filename);
        return false;
    }

    // Parse the PCX file
    pcx_t *pcx = reinterpret_cast<pcx_t *>(raw);
    raw = &pcx->data;

    pcx->xmin = LittleShort(pcx->xmin);
    pcx->ymin = LittleShort(pcx->ymin);
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);

    if (pcx->manufacturer != 0x0a
        || pcx->version != 5
        || pcx->encoding != 1
        || pcx->bits_per_pixel != 8
        || pcx->xmax >= 640
        || pcx->ymax >= 480) {
        logprint("LoadPCX: Failed to load '%s'. Unsupported PCX file.\n", filename);
        return false; //mxd
    }


    if (palette) {
        *palette = static_cast<uint8_t*>(malloc(768));
        memcpy(*palette, reinterpret_cast<uint8_t *>(pcx) + len - 768, 768);
    }

    if (width)
        *width = pcx->xmax + 1;
    if (height)
        *height = pcx->ymax + 1;

    if (!pixels)
        return true; // No target array specified, so skip reading pixels

    const int numbytes = (pcx->ymax + 1) * (pcx->xmax + 1); //mxd
    uint8_t *out = static_cast<uint8_t*>(malloc(numbytes));
    if (!out) {
        logprint("LoadPCX: Failed to load '%s'. Couldn't allocate %i bytes of memory.\n", filename, numbytes);
        return false; //mxd
    }

    *pixels = out;

    uint8_t *pix = out;

    for (int y = 0; y <= pcx->ymax; y++, pix += pcx->xmax + 1) {
        for (int x = 0; x <= pcx->xmax; ) {
            int dataByte = *raw++;

            if ((dataByte & 0xC0) == 0xC0) {
                runLength = dataByte & 0x3F;
                dataByte = *raw++;
            } else {
                runLength = 1;
            }

            while (runLength-- > 0)
                pix[x++] = dataByte;
        }
    }

    if (raw - reinterpret_cast<uint8_t *>(pcx) > len) {
        logprint("LoadPCX: File '%s' was malformed.\n", filename);
        return false; //mxd
    }

    free(pcx);

    return true;
}

/*
============================================================================
TARGA IMAGE
============================================================================
*/

//mxd. Copied from https://github.com/qbism/q2tools-220/blob/f8f582fc02196955584542c95de8a45a138c9e42/common/lbmlib.c#L625
typedef struct _TargaHeader {
    unsigned char 	id_length, colormap_type, image_type;
    unsigned short	colormap_index, colormap_length;
    unsigned char	colormap_size;
    unsigned short	x_origin, y_origin, width, height;
    unsigned char	pixel_size, attributes;
} TargaHeader;

int
fgetLittleShort(FILE *f)
{
    const uint8_t b1 = fgetc(f);
    const uint8_t b2 = fgetc(f);
    return static_cast<short>(b1 + b2 * 256);
}

int
fgetLittleLong(FILE *f)
{
    const uint8_t b1 = fgetc(f);
    const uint8_t b2 = fgetc(f);
    const uint8_t b3 = fgetc(f);
    const uint8_t b4 = fgetc(f);
    return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24);
}

/*
=============
LoadTGA
=============
*/
qboolean
LoadTGA(const char *filename, uint8_t **pixels, int *width, int *height)
{
    uint8_t			*pixbuf;
    int				row, column;
    TargaHeader		targa_header;

    FILE *fin = fopen(filename, "rb");
    if (!fin) {
        logprint("LoadTGA: Failed to load '%s'. File does not exist.\n", filename);
        return false; //mxd
    }

    targa_header.id_length = fgetc(fin);
    targa_header.colormap_type = fgetc(fin);
    targa_header.image_type = fgetc(fin);

    targa_header.colormap_index = fgetLittleShort(fin);
    targa_header.colormap_length = fgetLittleShort(fin);
    targa_header.colormap_size = fgetc(fin);
    targa_header.x_origin = fgetLittleShort(fin);
    targa_header.y_origin = fgetLittleShort(fin);
    targa_header.width = fgetLittleShort(fin);
    targa_header.height = fgetLittleShort(fin);
    targa_header.pixel_size = fgetc(fin);
    targa_header.attributes = fgetc(fin);

    if (targa_header.image_type != 2 && targa_header.image_type != 10) {
        logprint("LoadTGA: Failed to load '%s'. Only type 2 and 10 targa RGB images supported.\n", filename);
        return false; //mxd
    }

    if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24)) {
        logprint("LoadTGA: Failed to load '%s'. Only 32 or 24 bit images supported (no colormaps).\n", filename);
        return false; //mxd
    }

    const int columns = targa_header.width;
    const int rows = targa_header.height;
    const int numPixels = columns * rows;

    if (width)
        *width = columns;
    if (height)
        *height = rows;

    uint8_t *targa_rgba = static_cast<uint8_t*>(malloc(numPixels * 4));
    *pixels = targa_rgba;

    if (targa_header.id_length != 0)
        fseek(fin, targa_header.id_length, SEEK_CUR);  // skip TARGA image comment

    if (targa_header.image_type == 2) {  // Uncompressed, RGB images
        for (row = rows - 1; row >= 0; row--) {
            pixbuf = targa_rgba + row * columns * 4;
            for (column = 0; column < columns; column++) {
                unsigned char red, green, blue, alphabyte;
                switch (targa_header.pixel_size) {
                case 24:
                    blue = getc(fin);
                    green = getc(fin);
                    red = getc(fin);
                    *pixbuf++ = red;
                    *pixbuf++ = green;
                    *pixbuf++ = blue;
                    *pixbuf++ = 255;
                    break;
                case 32:
                    blue = getc(fin);
                    green = getc(fin);
                    red = getc(fin);
                    alphabyte = getc(fin);
                    *pixbuf++ = red;
                    *pixbuf++ = green;
                    *pixbuf++ = blue;
                    *pixbuf++ = alphabyte;
                    break;
                default:
                    logprint("LoadTGA: unsupported pixel size: %i\n", targa_header.pixel_size); //mxd
                    return false;
                }
            }
        }
    } else if (targa_header.image_type == 10) {   // Runlength encoded RGB images
        unsigned char red, green, blue, alphabyte, j;
        for (row = rows - 1; row >= 0; row--) {
            pixbuf = targa_rgba + row * columns * 4;
            for (column = 0; column<columns; ) {
                const unsigned char packetHeader = getc(fin);
                const unsigned char packetSize = 1 + (packetHeader & 0x7f);
                if (packetHeader & 0x80) {        // run-length packet
                    switch (targa_header.pixel_size) {
                    case 24:
                        blue = getc(fin);
                        green = getc(fin);
                        red = getc(fin);
                        alphabyte = 255;
                        break;
                    case 32:
                        blue = getc(fin);
                        green = getc(fin);
                        red = getc(fin);
                        alphabyte = getc(fin);
                        break;
                    default:
                        logprint("LoadTGA: unsupported pixel size: %i\n", targa_header.pixel_size); //mxd
                        return false;
                    }

                    for (j = 0; j<packetSize; j++) {
                        *pixbuf++ = red;
                        *pixbuf++ = green;
                        *pixbuf++ = blue;
                        *pixbuf++ = alphabyte;
                        column++;
                        if (column == columns) { // run spans across rows
                            column = 0;
                            if (row>0)
                                row--;
                            else
                                goto breakOut;
                            pixbuf = targa_rgba + row * columns * 4;
                        }
                    }
                } else {                         // non run-length packet
                    for (j = 0; j<packetSize; j++) {
                        switch (targa_header.pixel_size) {
                        case 24:
                            blue = getc(fin);
                            green = getc(fin);
                            red = getc(fin);
                            *pixbuf++ = red;
                            *pixbuf++ = green;
                            *pixbuf++ = blue;
                            *pixbuf++ = 255;
                            break;
                        case 32:
                            blue = getc(fin);
                            green = getc(fin);
                            red = getc(fin);
                            alphabyte = getc(fin);
                            *pixbuf++ = red;
                            *pixbuf++ = green;
                            *pixbuf++ = blue;
                            *pixbuf++ = alphabyte;
                            break;
                        default:
                            logprint("LoadTGA: unsupported pixel size: %i\n", targa_header.pixel_size); //mxd
                            return false;
                        }
                        column++;
                        if (column == columns) { // pixel packet run spans across rows
                            column = 0;
                            if (row>0)
                                row--;
                            else
                                goto breakOut;
                            pixbuf = targa_rgba + row * columns * 4;
                        }
                    }
                }
            }
        breakOut:;
        }
    }

    fclose(fin);

    return true; //mxd
}

/*
============================================================================
WAL IMAGE
============================================================================
*/

qboolean LoadWAL(const char *filename, uint8_t **pixels, int *width, int *height)
{
    if (FileTime(filename) == -1) {
        logprint("LoadWAL: Failed to load '%s'. File does not exist.\n", filename);
        return false; // Because LoadFile will throw an Error if the file doesn't exist... 
    }

    q2_miptex_t	*mt;
    const int len = LoadFile(filename, static_cast<void *>(&mt));
    if (len < 1) {
        logprint("LoadWAL: Failed to load '%s'. File is empty.\n", filename);
        return false;
    }

    const int w = LittleLong(mt->width);
    const int h = LittleLong(mt->height);
    const int offset = LittleLong(mt->offsets[0]);
    const int numbytes = w * h;
    const int numpixels = numbytes * 4; // RGBA

    if (width) *width = w;
    if (height) *height = h;

    uint8_t *out = static_cast<uint8_t*>(malloc(numpixels));
    if (!out) {
        logprint("LoadWAL: Failed to load '%s'. Couldn't allocate %i bytes of memory.\n", filename, numpixels);
        return false;
    }

    *pixels = out;

    for (int i = 0; i < numbytes; i++) {
        const int palindex = reinterpret_cast<uint8_t*>(mt)[offset + i];
        out[i * 4]     = thepalette[palindex * 3];
        out[i * 4 + 1] = thepalette[palindex * 3 + 1];
        out[i * 4 + 2] = thepalette[palindex * 3 + 2];
        out[i * 4 + 3] = (palindex == 255 ? 0 : 255); // Last palette index is transparent color
    }

    free(mt);

    return true;
}

/*
==============================================================================
Load (Quake 2) / Convert (Quake, Hexen 2) textures from paletted to RGBA (mxd)
==============================================================================
*/

static void
WriteRGBATextureData(mbsp_t *bsp, const std::vector<rgba_miptex_t*> &tex_mips, const std::vector<uint8_t*> &tex_bytes)
{
    // Step 1: create header and write it...
    const int headersize = 4 + tex_mips.size() * 4;
    dmiptexlump_t *miplmp = static_cast<dmiptexlump_t*>(malloc(headersize));
    miplmp->nummiptex = tex_mips.size();

    // Write data offsets to the header...
    int totalsize = headersize; // total size of miptex_t + palette bytes
    const int miptexsize = sizeof(rgba_miptex_t);
    for (unsigned int i = 0; i < tex_mips.size(); i++) {
        if (tex_mips[i] == nullptr) {
            miplmp->dataofs[i] = -1;
        } else {
            miplmp->dataofs[i] = totalsize;
            totalsize += miptexsize + (tex_mips[i]->width * tex_mips[i]->height) * 4; // RGBA
        }
    }

    // Step 2: write rgba_miptex_t and palette bytes to uint8_t array
    uint8_t *texdata, *texdatastart;
    texdata = texdatastart = static_cast<uint8_t*>(malloc(totalsize));
    memcpy(texdata, miplmp, headersize);
    texdata += headersize;

    for (unsigned int i = 0; i < tex_mips.size(); i++) {
        if (tex_mips[i] == nullptr)
            continue;

        // Write rgba_miptex_t
        memcpy(texdata, tex_mips[i], miptexsize);
        texdata += miptexsize;

        // Write RGBA pixels
        const int numpixels = (tex_mips[i]->width * tex_mips[i]->height) * 4; // RGBA
        memcpy(texdata, tex_bytes[i], numpixels);
        texdata += numpixels;
    }

    // Store in bsp->drgbatexdata...
    bsp->drgbatexdata = reinterpret_cast<dmiptexlump_t*>(texdatastart);
    bsp->rgbatexdatasize = totalsize;
}

static void AddTextureName(std::map<std::string, std::string> &texturenames, const char *texture)
{
    // See if an earlier texinfo allready got the value
    if (texturenames.find(texture) != texturenames.end())
        return;

    char path[4][1024];
    static const qboolean is_mod = Q_strcasecmp(gamedir, basedir);

    sprintf(path[0], "%stextures/%s.tga", gamedir, texture); // TGA, in mod dir...
    sprintf(path[1], "%stextures/%s.tga", basedir, texture); // TGA, in game dir...
    sprintf(path[2], "%stextures/%s.wal", gamedir, texture); // WAL, in mod dir...
    sprintf(path[3], "%stextures/%s.wal", basedir, texture); // WAL, in game dir...

    int c;
    for (c = 0; c < 4; c++) {
        // Skip paths at even indexes when running from game folder... 
        if ((is_mod || c % 2 == 0) && FileTime(path[c]) != -1) {
            texturenames[std::string{ texture }] = std::string{ path[c] };
            break;
        }
    }

    if (c == 4) {
        if (is_mod)
            logprint("WARNING: failed to find texture '%s'. Checked paths:\n'%s'\n'%s'\n'%s'\n'%s'\n", texture, path[0], path[1], path[2], path[3]);
        else
            logprint("WARNING: failed to find texture '%s'. Checked paths:\n'%s'\n'%s'\n", texture, path[0], path[2]);

        // Store to preserve offset... 
        texturenames[std::string{ texture }] = std::string{};
    }
}

static void // Loads textures from disk and stores them in bsp->drgbatexdata (Quake 2)
LoadTextures(mbsp_t *bsp)
{
    logprint("--- LoadTextures ---\n");

    if (bsp->texdatasize) {
        Error("ERROR: Expected an empty dtexdata lump...\n");
        return;
    }

    // Step 1: gather all loadable textures...
    std::map<std::string, std::string> texturenames; // <texture name, texture file path>
    for (int i = 0; i < bsp->numtexinfo; i++)
        AddTextureName(texturenames, bsp->texinfo[i].texture);

    // Step 2: gather textures used by _project_texture. Yes, this means parsing dentdata twice...
    auto entdicts = EntData_Parse(bsp->dentdata);
    for (auto &entdict : entdicts) {
        if (EntDict_StringForKey(entdict, "classname").find("light") == 0) {
            auto tex = EntDict_StringForKey(entdict, "_project_texture");
            if (!tex.empty()) AddTextureName(texturenames, tex.c_str());
        }
    }

    // Step 3: load and convert to miptex_t, store texturename indices...
    std::map<std::string, int> indicesbytexturename;
    std::vector<rgba_miptex_t*> tex_mips{};
    std::vector<uint8_t*> tex_bytes{};
    const int miptexsize = sizeof(rgba_miptex_t);
    int counter = 0;

    for (auto pair : texturenames) {
        // Store texturename index...
        indicesbytexturename[std::string{ pair.first }] = counter++;

        // Add nullptrs to keep texture index in case of load problems...
        tex_mips.push_back(nullptr);
        tex_bytes.push_back(nullptr);
        
        // Find file extension
        const int dpos = pair.second.rfind('.');
        if (dpos == -1) {
            if (!pair.second.empty()) // Missing texture warning was already displayed
                logprint("WARNING: unexpected texture filename: '%s'\n", pair.second.c_str());
            continue;
        }
        const std::string ext = pair.second.substr(dpos + 1);

        // Load images as RGBA
        int width, height;
        uint8_t *pixels;

        if (string_iequals(ext, "tga")) {
            if (!LoadTGA(pair.second.c_str(), &pixels, &width, &height)) 
                continue;
        } else if (string_iequals(ext, "wal")) {
            if (!LoadWAL(pair.second.c_str(), &pixels, &width, &height)) 
                continue;
        } else {
            logprint("WARNING: unsupported image format: '%s'\n", pair.second.c_str());
            continue;
        }

        // Create rgba_miptex_t...
        rgba_miptex_t *tex = static_cast<rgba_miptex_t*>(malloc(miptexsize));
        strcpy(tex->name, pair.first.c_str());
        tex->width = width;
        tex->height = height;
        tex->offset = miptexsize;

        // Replace nullptrs with actual data...
        tex_mips[tex_mips.size() - 1] = tex;
        tex_bytes[tex_bytes.size() - 1] = pixels;
    }

    // Sanity checks...
    Q_assert(tex_mips.size() == tex_bytes.size());
    Q_assert(texturenames.size() == tex_bytes.size());
    Q_assert(texturenames.size() == indicesbytexturename.size());

    // Step 4: write data to bsp...
    WriteRGBATextureData(bsp, tex_mips, tex_bytes);

    // Step 5: set miptex indices to gtexinfo_t
    for (int i = 0; i < bsp->numtexinfo; i++) {
        gtexinfo_t *info = &bsp->texinfo[i];

        const auto pair = indicesbytexturename.find(info->texture);
        if(pair != indicesbytexturename.end())
            info->miptex = pair->second;
    }
}

static void // Converts paletted bsp->dtexdata textures to RGBA bsp->drgbatexdata textures (Quake / Hexen2)
ConvertTextures(mbsp_t *bsp)
{
    if (!bsp->texdatasize) return;

    logprint("--- ConvertTextures ---\n");

    std::map<int, std::string> texturenamesbyindex;
    std::vector<rgba_miptex_t*> tex_mips{};
    std::vector<uint8_t*> tex_bytes{};
    const int miptexsize = sizeof(rgba_miptex_t);

    // Step 1: store texture data and RGBA bytes in temporary arrays...
    for (int i = 0; i < bsp->dtexdata->nummiptex; i++) {
        const int ofs = bsp->dtexdata->dataofs[i];

        // Pad to keep offsets...
        if (ofs < 0) {
            tex_mips.push_back(nullptr);
            tex_bytes.push_back(nullptr);
            continue;
        }

        miptex_t *miptex = (miptex_t *)((uint8_t *)bsp->dtexdata + ofs);

        // Create rgba_miptex_t...
        rgba_miptex_t *tex = static_cast<rgba_miptex_t*>(malloc(miptexsize));
        strcpy(tex->name, miptex->name);
        tex->width = miptex->width;
        tex->height = miptex->height;
        tex->offset = miptexsize;

        // Store texturename index...
        texturenamesbyindex[i] = std::string{ tex->name };

        // Convert to RGBA
        const int numpalpixels = tex->width * tex->height;
        uint8_t *pixels = static_cast<uint8_t*>(malloc(numpalpixels * 4)); //RGBA
        const uint8_t *data = reinterpret_cast<uint8_t*>(miptex) + miptex->offsets[0];

        for (int c = 0; c < numpalpixels; c++) {
            const uint8_t palindex = data[c];
            auto color = Palette_GetColor(palindex);
            for (int d = 0; d < 3; d++)
                pixels[c * 4 + d] = static_cast<uint8_t>(color[d]);
            pixels[c * 4 + 3] = static_cast<uint8_t>(palindex == 255 ? 0 : 255);
        }

        // Store...
        tex_mips.push_back(tex);
        tex_bytes.push_back(pixels);
    }

    // Sanity checks...
    Q_assert(tex_mips.size() == tex_bytes.size());
    Q_assert(bsp->dtexdata->nummiptex == tex_mips.size());

    // Step 2: write data to bsp...
    WriteRGBATextureData(bsp, tex_mips, tex_bytes);

    // Step 3: set texturenames to gmiptex_t
    for (int i = 0; i < bsp->numtexinfo; i++) {
        gtexinfo_t *info = &bsp->texinfo[i];

        const auto pair = texturenamesbyindex.find(info->miptex);
        if(pair != texturenamesbyindex.end())
            strcpy(info->texture, pair->second.c_str());
    }
}

void // Expects correct palette and game/mod paths to be set
LoadOrConvertTextures(mbsp_t *bsp)
{
    // Load or convert textures...
    if (bsp->loadversion == Q2_BSPVERSION)
        LoadTextures(bsp);
    else if (bsp->texdatasize > 0)
        ConvertTextures(bsp);
    else
        logprint("WARNING: failed to load or convert textures.\n");
}
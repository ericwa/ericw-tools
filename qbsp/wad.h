/*

WAD header file

*/

typedef struct
{
	char		identification[4];		// should be WAD2
	int			numlumps;
	int			infotableofs;
} wadinfo_t;

typedef struct
{
	int			filepos;
	int			disksize;
	int			size;					// uncompressed
	char		type;
	char		compression;
	char		pad1, pad2;
	char		name[16];				// must be null terminated
} lumpinfo_t;

typedef struct
{
	wadinfo_t header;
	lumpinfo_t *lumps;
	File Wad;
//	char szName[512];
} wadlist_t;

class WAD
{
public:
	WAD(void);
	~WAD(void);
	bool InitWADList(char *szWadlist);
	void fProcessWAD(void);

private:
	char *szWadName;
	wadlist_t *wadlist;
	int iWad, cWads;

	void LoadTextures(dmiptexlump_t *l);
	void InitWadFile(void);
	int LoadLump(char *szName, byte *pDest);
	void AddAnimatingTextures(void);
};
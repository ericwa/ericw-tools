/*

File header file

*/

#ifndef FILE_H
#define FILE_H

#include <stdio.h>

class File
{
public:
	File(void);
	~File(void);
	bool fOpen(char *szFile, char *szMode, bool fNoFail = true);
	void Close(void);
	int LoadFile(char *szFile, void **pBuffer, bool fNoFail = true);

	void Printf(char *szFormat, ...);
	void Read(void *pBuffer, int cLen);
	void Write(void *pBuffer, int cLen);
	int Seek(int Offset, int origin);
	int Position(void);

private:
	FILE *fp;

	int Length(void);
};

#endif
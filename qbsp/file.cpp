/*

File source file

*/

#include <stdarg.h>

#include "qbsp.h"
#include "file.h"

/*
==================
File
==================
*/

File::File(void)
{
	fp = NULL;
}


/*
==================
~File
==================
*/
File::~File(void)
{
	if (fp)
		fclose(fp);
}


/*
============
fOpen
============
*/

bool File::fOpen(char *szFilename, char *szMode, bool fNoFail)
{
	fp = fopen(szFilename, szMode);

	if (!fp)
		if (fNoFail)
			Message(msgError, errOpenFailed, szFilename, strerror(errno));
		else
			return false;

	return true;
}


/*
=============
Close
=============
*/
void File::Close(void)
{
	if (fp)
		fclose(fp);
	fp = NULL;
}


/*
==============
LoadFile
==============
*/
int File::LoadFile(char *szFile, void **ppBuffer, bool fNoFail)
{
	int cLen;

	if (!fOpen(szFile, "rb", fNoFail))
		return 0;

	cLen = Length();
	*ppBuffer = (char *)AllocMem(OTHER, cLen+1, false);
	((char *)*ppBuffer)[cLen] = 0;
	Read(*ppBuffer, cLen);

	Close();

	return cLen;
}


/*
==================
Printf
==================
*/
void File::Printf(char *szFormat, ...)
{
	char szBuffer[512];
	va_list argptr;

	if (!fp)
		return;

	va_start(argptr, szFormat);

	vsprintf(szBuffer, szFormat, argptr);
	if (fprintf(fp, "%s", szBuffer) < 0)
		Message(msgError, errWriteFailure);

	va_end(argptr);
}


/*
========
Read
========
*/
void File::Read(void *pBuffer, int cLen)
{
	// Fails silently if fp == NULL
	if (fp && fread(pBuffer, 1, cLen, fp) != (size_t)cLen)
		Message(msgError, errReadFailure);
}


/*
========
Write
========
*/
void File::Write(void *pBuffer, int cLen)
{
	// Fails silently if fp == NULL
	if (fp && fwrite(pBuffer, 1, cLen, fp) != (size_t)cLen)
		Message(msgError, errWriteFailure);
}


/*
=========
Seek
=========
*/
int File::Seek(int Offset, int origin)
{
	return fseek(fp, Offset, origin);
}


/*
=========
Position
=========
*/
int File::Position(void)
{
	return ftell(fp);
}
/*
==========
Length
==========
*/
int File::Length(void)
{
	int cCur;
	int cEnd;

	cCur = ftell(fp);
	fseek(fp, 0, SEEK_END);
	cEnd = ftell(fp);
	fseek(fp, cCur, SEEK_SET);

	return cEnd;
}
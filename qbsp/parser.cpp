/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/
/*

Parser source file

*/

#include "qbsp.h"
#include "parser.h"

/*
==================
Parser
==================
*/
Parser::Parser (char *data)
{
	iLineNum = 1;
	pScript = data;
	fUnget = false;
}


/*
==================
ParseToken
==================
*/
bool Parser::ParseToken (bool crossline)
{
	char    *token_p;

	if (fUnget)                         // is a token already waiting?
		return true;

	// skip space
skipspace:
	while (*pScript <= 32)
	{
		if (!*pScript)
		{
			if (!crossline)
				Message(msgError, errLineIncomplete, iLineNum);
			return false;
		}
		if (*pScript++ == '\n')
		{
			if (!crossline)
				Message(msgError, errLineIncomplete, iLineNum);
			iLineNum++;
		}
	}

	if (pScript[0] == '/' && pScript[1] == '/')	// comment field
	{
		if (!crossline)
			Message(msgError, errLineIncomplete, iLineNum);
		while (*pScript++ != '\n')
			if (!*pScript)
			{
				if (!crossline)
					Message(msgError, errLineIncomplete, iLineNum);
				return false;
			}
		goto skipspace;
	}

	// copy token
	token_p = szToken;

	if (*pScript == '"')
	{
		pScript++;
		while ( *pScript != '"' )
		{
			if (!*pScript)
				Message(msgError, errEOFInQuotes, iLineNum);
			*token_p++ = *pScript++;
			if (token_p > &szToken[MAXTOKEN-1])
				Message(msgError, errTokenTooLarge, iLineNum);
		}
		pScript++;
	}
	else while ( *pScript > 32 )
	{
		*token_p++ = *pScript++;
		if (token_p > &szToken[MAXTOKEN-1])
			Message(msgError, errTokenTooLarge, iLineNum);
	}

	*token_p = 0;
	
	return true;
}

/*
==================
UngetToken
==================
*/
void Parser::UngetToken ()
{
	fUnget = true;
}
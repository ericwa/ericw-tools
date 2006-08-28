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
/* scriplib.c */

#include <common/cmdlib.h>
#include <common/scriplib.h>

/*
 * ============================================================================
 *                                 PARSING STUFF
 * ============================================================================
 */

char token[MAXTOKEN];
char *scriptbuffer, *script_p, *scriptend_p;
int grabbed;
int scriptline;
qboolean endofscript;
qboolean tokenready;		/* only true if UnGetToken was just called */

/*
 * ==============
 * LoadScriptFile
 * ==============
 */
void
LoadScriptFile(char *filename)
{
    int size;

    size = LoadFile(filename, (void **)&scriptbuffer);

    script_p = scriptbuffer;
    scriptend_p = script_p + size;
    scriptline = 1;
    endofscript = false;
    tokenready = false;
}


/*
 * ==============
 * UnGetToken
 * Signals that the current token was not used, and should be reported
 * for the next GetToken.  Note that
 *
 *   GetToken (true);
 *   UnGetToken ();
 *   GetToken (false);
 *
 * could cross a line boundary.
 * ==============
 */
void
UnGetToken(void)
{
    tokenready = true;
}

/*
 * ==============
 * GetToken
 * ==============
 */
qboolean
GetToken(qboolean crossline)
{
    char *token_p;

    if (tokenready) {		/* is a token allready waiting? */
	tokenready = false;
	return true;
    }

    if (script_p >= scriptend_p) {
	if (!crossline)
	    Error("Line %i is incomplete\n", scriptline);
	endofscript = true;
	return false;
    }

    /* skip space */
  skipspace:
    while (*script_p <= 32) {
	if (script_p >= scriptend_p) {
	    if (!crossline)
		Error("Line %i is incomplete\n", scriptline);
	    endofscript = true;
	    return true;
	}
	if (*script_p++ == '\n') {
	    if (!crossline)
		Error("Line %i is incomplete\n", scriptline);
	    scriptline++;
	}
    }

    if (script_p >= scriptend_p) {
	if (!crossline)
	    Error("Line %i is incomplete\n", scriptline);
	endofscript = true;
	return true;
    }

    if (*script_p == ';' || *script_p == '#') {	/* semicolon is comment field */
	/* also make # comment field */
	if (!crossline)
	    Error("Line %i is incomplete\n", scriptline);
	while (*script_p++ != '\n')
	    if (script_p >= scriptend_p) {
		endofscript = true;
		return false;
	    }
	goto skipspace;
    }

    /* copy token */
    token_p = token;

    while (*script_p > 32 && *script_p != ';') {
	*token_p++ = *script_p++;
	if (script_p == scriptend_p)
	    break;
	if (token_p == &token[MAXTOKEN])
	    Error("Token too large on line %i\n", scriptline);
    }

    *token_p = 0;
    return true;
}


/*
 * ==============
 * TokenAvailable
 * Returns true if there is another token on the line
 * ==============
 */
qboolean
TokenAvailable(void)
{
    char *search_p;

    search_p = script_p;

    if (search_p >= scriptend_p)
	return false;

    while (*search_p <= 32) {
	if (*search_p == '\n')
	    return false;
	search_p++;
	if (search_p == scriptend_p)
	    return false;
    }

    if (*search_p == ';')
	return false;

    return true;
}

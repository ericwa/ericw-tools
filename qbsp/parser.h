/*

Parser header file

*/

#define MAXTOKEN 256

class Parser
{
public:
	Parser (char *data);
	bool ParseToken (bool crossline);
	void UngetToken (void);
	inline int GetLineNum (void) {return iLineNum;}
	inline char *GetToken (void) {return szToken;}

private:
	bool fUnget;
	char *pScript;
	int iLineNum;
	char szToken[MAXTOKEN];
};
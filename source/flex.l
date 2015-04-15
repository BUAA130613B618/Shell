%{
#include "bison.tab.h"
#include "global.h"
%}

%%

"<"		{ strcat(inputBuff, yytext); return LESS; }
">"		{ strcat(inputBuff, yytext); return GREATER; }
"&"		{ strcat(inputBuff, yytext); return AND; }
[ \t]		{ strcat(inputBuff, yytext); }
\n		{ return 0; }
[^ \t\n]+	{ strcat(inputBuff, yytext); return STRING; }

%%

int yywrap()
{
	return (1);
}


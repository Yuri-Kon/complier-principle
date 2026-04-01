%option noyywrap yylineno

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include "syntax.tab.h"
#include "tree.h"

int lexical_error = 0;
%}

DIGIT      [0-9]
NZDIGIT    [1-9]
LETTER     [A-Za-z_]
IDN        {LETTER}({LETTER}|{DIGIT})*
INT10      0|[1-9]{DIGIT}*
OCT        0[0-7]+
HEX        0[xX][0-9a-fA-F]*[g-zG-Z_][A-Za-z0-9_]*
BAD_OCT    0[0-9]*[89][0-9]*
BAD_HEX    0[xX][0-9a-fA-F]*[g-zG-Z_][A-Za-z0-9_]*
FLOATNUM   ({DIGIT}+"."{DIGIT}*|{DIGIT}*"."{DIGIT}+)
EXP        [eE][+-]?{DIGIT}+
FLOAT_EXP  ({FLOATNUM}|{INT10}){EXP}
BAD_EXP    ({FLOATNUM}|{INT10})[eE][+-]?
WS         [ \t\r]+

%%

"int"                       { yylval.node = new_type_node("TYPE", yylineno, "int"); return TYPE; }
"float"                     { yylval.node = new_type_node("TYPE", yylineno, "float"); return TYPE; }
"struct"                    { yylval.node = new_token_node("STRUCT", yylineno); return STRUCT; }
"return"                    { yylval.node = new_token_node("RETURN", yylineno); return RETURN; }
"if"                        { yylval.node = new_token_node("IF", yylineno); return IF; }
"else"                      { yylval.node = new_token_node("ELSE", yylineno); return ELSE; }
"while"                     { yylval.node = new_token_node("WHILE", yylineno); return WHILE; }

";"                         { yylval.node = new_token_node("SEMI", yylineno); return SEMI; }
","                         { yylval.node = new_token_node("COMMA", yylineno); return COMMA; }
"="                         { yylval.node = new_token_node("ASSIGNOP", yylineno); return ASSIGNOP; }
"=="|"!="|">="|"<="|">"|"<" { yylval.node = new_token_node("RELOP", yylineno); return RELOP; }
"+"                         { yylval.node = new_token_node("PLUS", yylineno); return PLUS; }
"-"                         { yylval.node = new_token_node("MINUS", yylineno); return MINUS; }
"*"                         { yylval.node = new_token_node("STAR", yylineno); return STAR; }
"/"                         { yylval.node = new_token_node("DIV", yylineno); return DIV; }
"&&"                        { yylval.node = new_token_node("AND", yylineno); return AND; }
"||"                        { yylval.node = new_token_node("OR", yylineno); return OR; }
"."                         { yylval.node = new_token_node("DOT", yylineno); return DOT; }
"!"                         { yylval.node = new_token_node("NOT", yylineno); return NOT; }
"("                         { yylval.node = new_token_node("LP", yylineno); return LP; }
")"                         { yylval.node = new_token_node("RP", yylineno); return RP; }
"["                         { yylval.node = new_token_node("LB", yylineno); return LB; }
"]"                         { yylval.node = new_token_node("RB", yylineno); return RB; }
"{"                         { yylval.node = new_token_node("LC", yylineno); return LC; }
"}"                         { yylval.node = new_token_node("RC", yylineno); return RC; }

{BAD_HEX}                   {
                                lexical_error = 1;
                                printf("Error type A at Line %d: Illegal hexadecimal number \"%s\".\n", yylineno, yytext);
                            }

{BAD_OCT}                   {
                                lexical_error = 1;
                                printf("Error type A at Line %d: Illegal octal number \"%s\".\n", yylineno, yytext);
                            }

{FLOAT_EXP}                 {
                                yylval.node = new_float_node("FLOAT", yylineno, (float)atof(yytext));
                                return FLOAT;
                            }

{BAD_EXP}                   {
                                lexical_error = 1;
                                printf("Error type A at Line %d: Illegal floating point number \"%s\".\n", yylineno, yytext);
                            }

{HEX}                       {
                                yylval.node = new_int_node("INT", yylineno, (int)strtol(yytext, NULL, 16));
                                return INT;
                            }

{OCT}                       {
                                yylval.node = new_int_node("INT", yylineno, (int)strtol(yytext, NULL, 8));
                                return INT;
                            }

{FLOATNUM}                  {
                                yylval.node = new_float_node("FLOAT", yylineno, (float)atof(yytext));
                                return FLOAT;
                            }

{INT10}                     {
                                yylval.node = new_int_node("INT", yylineno, atoi(yytext));
                                return INT;
                            }

{IDN}                       {
                                yylval.node = new_id_node("ID", yylineno, yytext);
                                return ID;
                            }

\n                          { }
{WS}                        { }

.                           {
                                lexical_error = 1;
                                printf("Error type A at Line %d: Mysterious character \"%s\".\n", yylineno, yytext);
                            }
%%

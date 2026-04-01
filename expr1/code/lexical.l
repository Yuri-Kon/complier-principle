%option noyywrap yylineno noinput nounput

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "syntax.tab.h"
#include "tree.h"

YYLTYPE yyloc;
  
int lexical_error = 0;
int last_error_line = -1;

static float parse_float_literal(const char *text) {
  char buf[128];
  int j = 0;

  for (int i = 0; text[i] != '\0' && j < (int)sizeof(buf) - 1; ++i) {
    if (!isspace((unsigned char)text[i])) {
      buf[j++] = text[i];
    }
  }
  buf[j] = '\0';
  return (float)atof(buf);
}

%}

%x COMMENT SKIPLINE

DIGIT      [0-9]
NZDIGIT    [1-9]
LETTER     [A-Za-z_]
IDN        {LETTER}({LETTER}|{DIGIT})*

INT10      0|{NZDIGIT}{DIGIT}*
OCT        0[0-7]+
HEX        0[xX][0-9a-fA-F]+

BAD_OCT    0[0-9]*[89][0-9]*
BAD_HEX    0[xX][0-9a-fA-F]*[g-zG-Z_][A-Za-z0-9_]*

FLOATNUM   ({DIGIT}+"."{DIGIT}*|{DIGIT}*"."{DIGIT}+)
EXP        [eE][+-]?{DIGIT}+
EXP_WS     [eE][ \t\r]*[+-]?[ \t\r]*{DIGIT}+
FLOAT_EXP  ({FLOATNUM}|{INT10})({EXP}|{EXP_WS})
BAD_EXP    ({FLOATNUM}|{INT10})[eE][ \t\r]*[+-]?[ \t\r]*

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

"//"[^\n]*                  { /* 忽略单行注释 */ }
"/*"                        { BEGIN(COMMENT); }
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
                                if (last_error_line != yylineno) {
                                  printf("Error type A at Line %d: Illegal hexadecimal number \"%s\".\n", yylineno, yytext);
                                  last_error_line = yylineno;}
                                BEGIN(SKIPLINE);
                                yylval.node = new_int_node("INT", yylineno, 0);
                            }

{BAD_OCT}                   {
                                lexical_error = 1;
                                if (last_error_line != yylineno) {
                                  printf("Error type A at Line %d: Illegal octal number \"%s\".\n", yylineno, yytext);
                                  last_error_line = yylineno;
                                }
                                BEGIN(SKIPLINE);
                                yylval.node = new_int_node("INT", yylineno, 0);
                                return INT;
                            }

{FLOAT_EXP}                 {
                                yylval.node = new_float_node("FLOAT", yylineno, parse_float_literal(yytext));
                                return FLOAT;
                            }

{BAD_EXP}                   {
                                lexical_error = 1;
                                if (last_error_line != yylineno) {
                                  printf("Error type A at Line %d: Illegal floating point number \"%s\".\n", yylineno, yytext);
                                  last_error_line = yylineno;
                                }
                                BEGIN(SKIPLINE);
                                yylval.node = new_int_node("FLOAT", yylineno, 0.0f);
                                return FLOAT;
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
                                if (last_error_line != yylineno) {
                                  printf("Error type A at Line %d: Mysterious character \"%s\".\n", yylineno, yytext);
                                  last_error_line = yylineno;
                                }
                                BEGIN(SKIPLINE);
                                yylval.node = new_int_node("INT", yylineno, 0);
                                return INT;
                            }

<COMMENT>"*/"               { BEGIN(INITIAL); }
<COMMENT>\n                 { }
<COMMENT>.                  { }
<COMMENT><<EOF>>            {
                                lexical_error = 1;
                                if (last_error_line != yylineno) {
                                    printf("Error type A at Line %d: Unterminated comment.\n", yylineno);
                                    last_error_line = yylineno;
                                }
                                BEGIN(INITIAL);
                                return 0;
                            }
<SKIPLINE>[^;\n]+    { }
<SKIPLINE>";"        { BEGIN(INITIAL); yylval.node = new_token_node("SEMI", yylineno); return SEMI; }
<SKIPLINE>\n         { BEGIN(INITIAL); }

%%

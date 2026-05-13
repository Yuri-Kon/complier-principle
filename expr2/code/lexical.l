%option noyywrap yylineno noinput nounput

%{
/*
 * lexical.l 负责把源文件字符流切分成 token。
 * 这里除了基本 token 识别，还承担三件与实验要求直接相关的工作：
 * 1. 构造语法树叶子结点；
 * 2. 输出词法错误 Error type A；
 * 3. 过滤注释、为语法分析阶段提供更干净的 token 流。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "syntax.tab.h"
#include "tree.h"

/* Bison 开启 %locations 后，词法分析器也能共享位置信息类型。 */
YYLTYPE yyloc;
  
/* lexical_error 控制 main.c 是否打印语法树；last_error_line 防止同一行重复报错。 */
int lexical_error = 0;
int last_error_line = -1;

/*
 * 当前测试中既有 1.05e-4，也有 1.05e - 4。
 * 为了兼容两种写法，这里在转换为数值前先去掉中间的空白字符。
 */
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

/* COMMENT: 处理块注释；SKIPLINE: 发生词法错误后跳到本行末尾，避免重复报错。 */
%x COMMENT SKIPLINE

/* ---------- 基本词法模式 ---------- */
DIGIT      [0-9]
NZDIGIT    [1-9]
LETTER     [A-Za-z_]
IDN        {LETTER}({LETTER}|{DIGIT})*

/* 十进制、八进制、十六进制整数。 */
INT10      0|{NZDIGIT}{DIGIT}*
OCT        0[0-7]+
HEX        0[xX][0-9a-fA-F]+

/* 选做要求：把非法八/十六进制直接识别出来并报 Error type A。 */
BAD_OCT    0[0-9]*[89][0-9]*
BAD_HEX    0[xX][0-9a-fA-F]*[g-zG-Z_][A-Za-z0-9_]*

/* 选做要求：指数浮点数及其错误形式。 */
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
                                /* 进入 SKIPLINE，跳过本行剩余内容。 */
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

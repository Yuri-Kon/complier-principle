%code requires {
    #include "tree.h"
}

%{
#include <stdio.h>
#include "tree.h"

extern int yylex(void);
extern int yylineno;
extern FILE* yyin;
extern int last_error_line;

TreeNode* root = NULL;
int has_error = 0;

void yyerror(const char* msg);
static TreeNode* make_node(const char* name, int line) {
    return new_nonterminal(name,line);
}

static int first_line(TreeNode* a, TreeNode* b, TreeNode* c, TreeNode* d) {
    if (a) return a->line;
    if (b) return b->line;
    if (c) return c->line;
    if (d) return d->line;
    return 0;
}
%}

%union {
    TreeNode* node;
}

%token<node> ID TYPE INT FLOAT 
%token<node> SEMI COMMA ASSIGNOP RELOP
%token<node> PLUS MINUS STAR DIV
%token<node> AND OR DOT NOT
%token<node> LP RP LB RB LC RC
%token<node> STRUCT RETURN IF ELSE WHILE
%type <node> Program ExtDefList ExtDef Specifier StructSpecifier FunDec CompSt
%type <node> DefList Def DecList Dec VarDec
%type <node> StmtList Stmt Exp
%type <node> OptTag Tag

%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left STAR DIV
%left PLUS
%right NOT
%right UMINUS
%left LB RB DOT
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

Program
    : ExtDefList
      {
        $$ = make_node("Program", first_line($1, NULL, NULL, NULL));
        add_child($$, $1);
        root = $$;
      }
    ;

ExtDefList
    : ExtDef ExtDefList
      {
        $$ = make_node("ExtDefList", first_line($1, $2, NULL, NULL));
        add_child($$, $1);
        add_child($$, $2);
        
      }
    | /* empty */
      {
        $$ = NULL;
      }
    ;

ExtDef
    : Specifier FunDec CompSt
      {
        $$ = make_node("ExtDef", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
      }
    | Specifier SEMI
      {
        $$ = make_node("ExtDef", first_line($1, $2, NULL, NULL));
        add_child($$, $1);
        add_child($$, $2);
      }
    ;
Specifier
    : TYPE
      {
        $$ = make_node("Specifier", first_line($1, NULL, NULL, NULL));
        add_child($$, $1);  
      }
    | StructSpecifier
      {
        $$ = make_node("Specifier", first_line($1, NULL, NULL, NULL));
        add_child($$, $1);
      }
    ;
StructSpecifier
    : STRUCT OptTag LC DefList RC
      {
        $$ = make_node("StructSpecifier", first_line($1, $2, $3, $4));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
        add_child($$, $4);
        add_child($$, $5);
       }
         | STRUCT Tag
      {
          $$ = make_node("StructSpecifier", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
    ;

OptTag
    : ID
      {
          $$ = make_node("OptTag", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | /* empty */
      {
          $$ = NULL;
      }
    ;

Tag
    : ID
      {
          $$ = make_node("Tag", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    ;

FunDec
    : ID LP RP
      {
        $$ = make_node("FunDec", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
      }
    ;
CompSt
    : LC DefList StmtList RC
      {
          $$ = make_node("CompSt", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
      }
    ;

DefList
    : Def DefList
      {
          $$ = make_node("DefList", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
    | /* empty */
      {
          $$ = NULL;
      }
    ;

Def
    : Specifier DecList SEMI
      {
          $$ = make_node("Def", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    ;

DecList
    : Dec
      {
          $$ = make_node("DecList", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | Dec COMMA DecList
      {
        $$ = make_node("DecList", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
    }
    ;

Dec
    : VarDec
      {
          $$ = make_node("Dec", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
	| VarDec ASSIGNOP Exp
	  {
		$$ = make_node("Dec", first_line($1, $2, $3, NULL));
		add_child($$, $1);
		add_child($$, $2);
		add_child($$, $3);
	  }
	;

VarDec
    : ID
      {
          $$ = make_node("VarDec", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | VarDec LB INT RB
      {
          $$ = make_node("VarDec", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
      }
    ;

StmtList
    : Stmt StmtList
      {
          $$ = make_node("StmtList", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
    | /* empty */
      {
          $$ = NULL;
      }
    ;

Stmt
    : Exp SEMI
      {
          $$ = make_node("Stmt", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
      | RETURN Exp SEMI {
        $$ = make_node("Stmt", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
	add_child($$, $3);
      };
      | CompSt
       {
         $$ = make_node("Stmt", first_line($1, NULL, NULL, NULL));
         add_child($$, $1);      
       }
      | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
       {
          $$ = make_node("Stmt", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
          add_child($$, $5);       
       }
    | IF LP Exp RP Stmt ELSE Stmt
      {
          $$ = make_node("Stmt", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
          add_child($$, $5);
          add_child($$, $6);
          add_child($$, $7);
      }
    | WHILE LP Exp RP Stmt
      {
          $$ = make_node("Stmt", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
          add_child($$, $5);
      }
    ;

Exp
    : ID
      {
          $$ = make_node("Exp", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | INT
      {
          $$ = make_node("Exp", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | FLOAT
      {
          $$ = make_node("Exp", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    | Exp ASSIGNOP Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp OR Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp AND Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp RELOP Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp PLUS Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp STAR Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp DIV Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | LP Exp RP
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | MINUS Exp %prec UMINUS
      {
          $$ = make_node("Exp", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
    | NOT Exp
      {
          $$ = make_node("Exp", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
      }
    | Exp LB Exp RB
      {
          $$ = make_node("Exp", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
      }
    | Exp DOT ID
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    ;
%%

void yyerror(const char* msg) {
    has_error = 1;
    if (last_error_line != yylineno) {
      printf("Error type B at Line %d: %s.\n", yylineno, msg);
      last_error_line = yylineno;
    }
}

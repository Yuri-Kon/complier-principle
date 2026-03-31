%code requires {
    #include "tree.h"
}

%{
#include <stdio.h>
#include "tree.h"

extern int yylex(void);
extern int yylineno;
extern FILE* yyin;

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

%token <node> ID TYPE INT FLOAT
%token <node> SEMI COMMA ASSIGNOP RELOP
%token <node> PLUS MINUS STAR DIV
%token <node> AND OR DOT NOT
%token <node> LP RP LB RB LC RC
%token <node> STRUCT RETURN IF ELSE WHILE

%type <node> Program ExtDefList ExtDef Specifier FunDec CompSt
%type <node> DefList Def DecList Dec VarDec
%type <node> StmtList Stmt Exp

%right ASSIGNOP
%left PLUS

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
    ;
Specifier
    : TYPE
      {
        $$ = make_node("Specifier", first_line($1, NULL, NULL, NULL));
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
    | FLOAT {
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
    | Exp PLUS Exp
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
    }
    | LP Exp RP {
      $$ = make_node("Exp", first_line($1, $2, $3, NULL));
      add_child($$, $1);
      add_child($$, $2);
      add_child($$, $3);
    }
    | MINUS Exp %prec UMINUS {
      $$ = make_node("Exp", first_line($1, $2, NULL, NULL));
      add_child($$, $1);
      add_child($$, $2);
    }
;
%%

void yyerror(const char* msg) {
    has_error = 1;
    printf("Error type B at Line %d: %s.\n", yylineno, msg);
}

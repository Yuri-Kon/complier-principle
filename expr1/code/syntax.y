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
static void report_type_b(int line, const char* msg) {
    has_error = 1;
    if (last_error_line != line) {
        printf("Error type B at Line %d: %s.\n", line, msg);
        last_error_line = line;
    }
}

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

static TreeNode* make_exp_stmt(TreeNode* exp) {
    TreeNode* stmt = make_node("Stmt", first_line(exp, NULL, NULL, NULL));
    TreeNode* semi = new_token_node("SEMI", first_line(exp, NULL, NULL, NULL));
    add_child(stmt, exp);
    add_child(stmt, semi);
    return stmt;
}

static TreeNode* make_array_access(TreeNode* base, TreeNode* index) {
    int line = first_line(base, index, NULL, NULL);
    TreeNode* exp = make_node("Exp", line);
    TreeNode* lb = new_token_node("LB", line);
    TreeNode* rb = new_token_node("RB", line);
    add_child(exp, base);
    add_child(exp, lb);
    add_child(exp, index);
    add_child(exp, rb);
    return exp;
}
%}


%locations
    
%union {
    TreeNode* node;
}

%token<node> ID TYPE INT FLOAT 
%token<node> SEMI COMMA ASSIGNOP RELOP
%token<node> PLUS MINUS STAR DIV
%token<node> AND OR DOT NOT
%token<node> LP RP LB RB LC RC
%token<node> STRUCT RETURN IF ELSE WHILE
%type <node> Program ExtDefList ExtDef ExtDecList Specifier StructSpecifier FunDec CompSt
%type <node> VarList ParamDec DefList Def DecList Dec VarDec
%type <node> StmtList Stmt Exp Args
%type <node> OptTag Tag

%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left STAR DIV
%left PLUS MINUS
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
    : Specifier ExtDecList SEMI
      {
        $$ = make_node("ExtDef", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
      }
    | Specifier ExtDecList error
      {
        report_type_b(@3.first_line, "Missing \";\"");
        $$ = make_node("ExtDef", first_line($1, $2, NULL, NULL));
        add_child($$, $1);
        add_child($$, $2);
        yyerrok;
      }
    | Specifier FunDec CompSt
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

ExtDecList
    : VarDec
      {
        $$ = make_node("ExtDecList", first_line($1, NULL, NULL, NULL));
        add_child($$, $1);
      }
    | VarDec COMMA ExtDecList
      {
        $$ = make_node("ExtDecList", first_line($1, $2, $3, NULL));
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
    | ID LP VarList RP
      {
        $$ = make_node("FunDec", first_line($1, $2, $3, $4));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
        add_child($$, $4);
      }
    ;

VarList
    : ParamDec COMMA VarList
      {
        $$ = make_node("VarList", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
      }
    | ParamDec
      {
        $$ = make_node("VarList", first_line($1, NULL, NULL, NULL));
        add_child($$, $1);
      }
    ;

ParamDec
    : Specifier VarDec
      {
        $$ = make_node("ParamDec", first_line($1, $2, NULL, NULL));
        add_child($$, $1);
        add_child($$, $2);
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
    | LC DefList error RC
      {
         $$ = make_node("CompSt", first_line($1, $2, $4, NULL));
         add_child($$, $1);
         add_child($$, $2);
         add_child($$, $4);
         yyerror("syntax error");
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
    | Specifier DecList error
      {
          report_type_b(@3.first_line, "Missing \";\"");
          $$ = make_node("Def", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
          yyerrok;
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
    | VarDec LB INT error
      {
          report_type_b(@4.first_line, "Missing \"]\"");
          $$ = make_node("VarDec", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          yyerrok;
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
    | RETURN Exp SEMI
      {
        $$ = make_node("Stmt", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
      }
    | RETURN Exp error
      {
        report_type_b(@3.first_line, "Missing \";\"");
        $$ = make_node("Stmt", first_line($1, $2, NULL, NULL));
        add_child($$, $1);
        add_child($$, $2);
        yyerrok;
      }
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
    | IF LP Exp error Stmt %prec LOWER_THAN_ELSE
      {
          report_type_b(@4.first_line, "Missing \")\"");
          $$ = make_node("Stmt", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $5);
          yyerrok;
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
    | IF LP Exp error Stmt ELSE Stmt
      {
          report_type_b(@4.first_line, "Missing \")\"");
          $$ = make_node("Stmt", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $5);
          add_child($$, $6);
          add_child($$, $7);
          yyerrok;
      }
    | IF LP Exp RP Exp ELSE Stmt
      {
          report_type_b($6->line, "Missing \";\"");
          TreeNode* then_stmt = make_exp_stmt($5);
          $$ = make_node("Stmt", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
          add_child($$, then_stmt);
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
    | WHILE LP Exp error Stmt
      {
          report_type_b(@4.first_line, "Missing \")\"");
          $$ = make_node("Stmt", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $5);
          yyerrok;
      }
    | error SEMI
      {
          $$ = NULL;
          yyerror("syntax error");
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
    | Exp MINUS Exp
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
    | ID LP Args RP
      {
          $$ = make_node("Exp", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
      }
    | ID LP Args error
      {
          report_type_b(@4.first_line, "Missing \")\"");
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          yyerrok;
      }
    | ID LP RP
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | ID LP error
      {
          report_type_b(@3.first_line, "Missing \")\"");
          $$ = make_node("Exp", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
          yyerrok;
      }
    | Exp LB Exp RB
      {
          $$ = make_node("Exp", first_line($1, $2, $3, $4));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
          add_child($$, $4);
      }
    | Exp LB Exp COMMA Exp RB
      {
          report_type_b($4->line, "Missing \"]\"");
          TreeNode* first_index = make_array_access($1, $3);
          $$ = make_array_access(first_index, $5);
      }
    | Exp DOT ID
      {
          $$ = make_node("Exp", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp LB error RB
      {
          $$ = make_node("Exp", first_line($1, $2, $4, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $4);
          yyerror("syntax error");
      }
    | Exp LB Exp error
      {
        if (last_error_line != @4.first_line) {
          printf("Error type B at Line %d: Missing \"]\".\n", @4.first_line);
          last_error_line = @4.first_line;
        }
        $$ = make_node("Exp", first_line($1, $2, $3, NULL));
        add_child($$, $1);
        add_child($$, $2);
        add_child($$, $3);
        yyerror("syntax error");
      }
    | LP Exp error
      {
          report_type_b(@3.first_line, "Missing \")\"");
          $$ = make_node("Exp", first_line($1, $2, NULL, NULL));
          add_child($$, $1);
          add_child($$, $2);
          yyerrok;
      }
    ;

Args
    : Exp COMMA Args
      {
          $$ = make_node("Args", first_line($1, $2, $3, NULL));
          add_child($$, $1);
          add_child($$, $2);
          add_child($$, $3);
      }
    | Exp
      {
          $$ = make_node("Args", first_line($1, NULL, NULL, NULL));
          add_child($$, $1);
      }
    ;
%%

void yyerror(const char* msg) {
    report_type_b(yylineno, msg);
}

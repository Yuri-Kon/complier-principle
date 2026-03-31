#include "tree.h"
#include <stdio.h>

extern FILE *yyin;
extern int yyparse(void);
extern TreeNode *root;
extern int has_error;
extern int lexical_error;

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return 1;
  }

  yyin = fopen(argv[1], "r");
  if (!yyin) {
    perror(argv[1]);
    return 1;
  }

  yyparse();

  if (!has_error && !lexical_error && root) {
    print_tree(root, 0);
  }

  fclose(yyin);
  return 0;
}

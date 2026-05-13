#include "tree.h"
#include "semantic.h"
#include <stdio.h>

/* 这些全局变量由 Flex/Bison 生成或在 syntax.y 中定义。 */
extern FILE *yyin;
extern int yyparse(void);
extern TreeNode *root;
extern int has_error;
extern int lexical_error;

int main(int argc, char **argv) {
  // 程序接收一个源文件路径作为唯一参数
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return 1;
  }

  /* 将输入文件交给 Flex/Bison 共享的输入流 yyin。 */
  yyin = fopen(argv[1], "r");
  if (!yyin) {
    perror(argv[1]);
    return 1;
  }

  /*
   * yyparse() 会不断调用 yylex()，因此词法分析和语法分析都在这里完成。
   * 若没有错误，syntax.y 会把 Program 根节点保存到全局变量 root。
   */
  yyparse();

  /*
   * 实验二不再打印语法树，而是在语法树上做语义分析。
   * 如果前面已经出现词法/语法错误，root 可能不完整，此时跳过语义分析。
   */
  if (!has_error && !lexical_error && root) {
    semantic_analyze(root);
  }

  fclose(yyin);
  return 0;
}

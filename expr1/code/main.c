#include "tree.h"
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

  /* 词法分析和语法分析都在 yyparse() 内部驱动完成。 */
  yyparse();

  /*
   * 只要出现词法错误或语法错误，就不能输出语法树。
   * 因此这里只在 无错误 + 成功构树 时打印先序遍历结果。
   */
  if (!has_error && !lexical_error && root) {
    print_tree(root, 0);
  }

  fclose(yyin);
  return 0;
}

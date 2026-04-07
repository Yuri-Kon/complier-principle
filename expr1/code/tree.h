#ifndef TREE_H
#define TREE_H

/*
 * 语法树采用 长子 + 兄弟 表示法：
 * child 指向第一个孩子，sibling 串起同层剩余结点。
 * 这种结构适合 Bison 在归约时按顺序逐个挂接孩子结点。
 */
typedef struct TreeNode {
  /* name 保存词法单元名或非终结符名，比如 ID / Exp / Stmt。 */
  char name[32];
  /* line 是该结点在源程序中第一次出现的行号。 */
  int line;
  /* is_token=1 表示词法单元，0 表示语法单元。 */
  int is_token;

  /* text 只对 ID / TYPE 等带字符串内容的 token 有意义。 */
  char text[64];
  /* int_val 和  float_val 分别用于 INT / FLOAT。 */
  int int_val;
  float float_val;

  struct TreeNode *child;
  struct TreeNode *sibling;
} TreeNode;

/* 各类工厂函数负责创建不同类型的语法树结点。 */
TreeNode *new_nonterminal(const char *name, int line);
TreeNode *new_type_node(const char *name, int line, const char *text);
TreeNode *new_token_node(const char *name, int line);
TreeNode *new_id_node(const char *name, int line, const char *text);
TreeNode *new_int_node(const char *name, int line, int val);
TreeNode *new_float_node(const char *name, int line, float val);

/* add_child 按从左到右的顺序挂接孩子 */
void add_child(TreeNode *parent, TreeNode *child);
/* print_tree 按实验要求输出。*/
void print_tree(TreeNode *root, int indent);
#endif

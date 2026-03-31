#ifndef TREE_H
#define TREE_H
typedef struct TreeNode {
  char name[32];
  int line;
  int is_token;
  char text[64];
  int int_val;
  float float_val;

  struct TreeNode *child;
  struct TreeNode *sibling;
} TreeNode;

TreeNode *new_nonterminal(const char *name, int line);
TreeNode *new_type_node(const char *name, int line, const char *text);
TreeNode *new_token_node(const char *name, int line);
TreeNode *new_id_node(const char *name, int line, const char *text);
TreeNode *new_int_node(const char *name, int line, int val);
TreeNode *new_float_node(const char *name, int line, float val);

void add_child(TreeNode *parent, TreeNode *child);
void print_tree(TreeNode *root, int indent);
#endif

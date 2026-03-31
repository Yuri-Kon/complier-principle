#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TreeNode *alloc_node(const char *name, int line, int is_token) {
  TreeNode *p = (TreeNode *)malloc(sizeof(TreeNode));
  if (!p) {
    return NULL;
  }
  memset(p, 0, sizeof(TreeNode));
  strncpy(p->name, name, sizeof(p->name) - 1);
  p->line = line;
  p->is_token = is_token;

  return p;
}

TreeNode *new_nonterminal(const char *name, int line) {
  return alloc_node(name, line, 0);
}

TreeNode *new_type_node(const char *name, int line, const char *text) {
  TreeNode *p = alloc_node(name, line, 1);
  if (p && text) {
    strncpy(p->text, text, sizeof(p->text) - 1); 
  }
  return p;
}

TreeNode *new_token_node(const char *name, int line) {
  return alloc_node(name, line, 1);
}

TreeNode *new_id_node(const char *name, int line, const char *text) {
  TreeNode *p = alloc_node(name, line, 1);
  if (p && text) {
    strncpy(p->text, text, sizeof(p->text) - 1);
  }

  return p;
}

TreeNode *new_int_node(const char *name, int line, int val) {
  TreeNode *p = alloc_node(name, line, 1);
  if (p)
    p->int_val = val;
  return p;
}

TreeNode *new_float_node(const char *name, int line, float val) {
  TreeNode *p = alloc_node(name, line, 1);
  if (p) {
    p->float_val = val;
  }
  return p;
}

void add_child(TreeNode *parent, TreeNode *child) {
  if (!parent || !child) {
    return;
  }

  if (!parent->child) {
    parent->child = child;
    return;
  }

  TreeNode *p = parent->child;
  while (p->sibling) {
    p = p->sibling;
  }
  p->sibling = child;
}

void print_tree(TreeNode *root, int indent) {
  if (!root) {
    return;
  }

  for (int i = 0; i < indent; ++i) {
    putchar(' ');
  }
  if (!root->is_token) {
    printf("%s (%d)\n", root->name, root->line);
  } else {
    if (strcmp(root->name, "ID") == 0 || strcmp(root->name, "TYPE") == 0) {
      printf("%s: %s\n", root->name, root->text);
    } else if (strcmp(root->name, "INT") == 0) {
      printf("%s: %d\n", root->name, root->int_val);
    } else if (strcmp(root->name, "FLOAT") == 0) {
      printf("%s: %f\n", root->name, root->float_val);
    } else {
      printf("%s\n", root->name);
    }
  }

  TreeNode *c = root->child;
  while (c) {
    print_tree(c, indent + 2);
    c = c->sibling;
  }
}

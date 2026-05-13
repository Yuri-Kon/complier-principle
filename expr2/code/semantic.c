#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_SIZE 16384

typedef struct Type Type;
typedef struct Field Field;
typedef struct VarSymbol VarSymbol;
typedef struct Scope Scope;
typedef struct Function Function;
typedef struct StructSymbol StructSymbol;
typedef struct NameItem NameItem;

enum TypeKind { TYPE_BASIC, TYPE_ARRAY, TYPE_STRUCT, TYPE_ERROR };
enum BasicKind { BASIC_INT, BASIC_FLOAT };

struct Type {
  enum TypeKind kind;
  union {
    int basic;
    struct {
      Type *elem;
      int size;
    } array;
    struct {
      char *name;
      Field *fields;
    } structure;
  } u;
};

struct Field {
  char *name;
  Type *type;
  int line;
  Field *next;
};

struct VarSymbol {
  char *name;
  Type *type;
  int depth;
  VarSymbol *bucket_next;
  VarSymbol *scope_next;
};

struct Scope {
  int depth;
  VarSymbol *symbols;
  Scope *prev;
};

struct Function {
  char *name;
  Type *ret;
  Field *params;
  int param_count;
  int defined;
  int decl_line;
  Function *next;
};

struct StructSymbol {
  char *name;
  Type *type;
  int line;
  StructSymbol *next;
};

struct NameItem {
  char *name;
  NameItem *next;
};

typedef struct {
  Type *type;
  int is_lvalue;
} ExprInfo;

static VarSymbol *var_table[HASH_SIZE];
static Scope *current_scope = NULL;
static int current_depth = 0;
static Function *functions = NULL;
static StructSymbol *structs = NULL;
static NameItem *seen_vars = NULL;
static Type *int_type = NULL;
static Type *float_type = NULL;
static Type *error_type = NULL;

static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  memset(p, 0, size);
  return p;
}

static char *xstrdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *p = (char *)xmalloc(len);
  memcpy(p, s, len);
  return p;
}

static unsigned hash_pjw(const char *name) {
  unsigned val = 0;
  unsigned i = 0;
  for (; *name; ++name) {
    val = (val << 2) + (unsigned char)(*name);
    if ((i = val & ~0x3fffU)) {
      val = (val ^ (i >> 12)) & 0x3fffU;
    }
  }
  return val;
}

static int is_node(TreeNode *node, const char *name) {
  return node && strcmp(node->name, name) == 0;
}

static TreeNode *child_at(TreeNode *node, int index) {
  TreeNode *cur = node ? node->child : NULL;
  for (int i = 0; cur && i < index; ++i) {
    cur = cur->sibling;
  }
  return cur;
}

static int child_count(TreeNode *node) {
  int count = 0;
  for (TreeNode *cur = node ? node->child : NULL; cur; cur = cur->sibling) {
    ++count;
  }
  return count;
}

static TreeNode *find_child(TreeNode *node, const char *name) {
  for (TreeNode *cur = node ? node->child : NULL; cur; cur = cur->sibling) {
    if (is_node(cur, name)) {
      return cur;
    }
  }
  return NULL;
}

static void semantic_error(int type, int line, const char *msg) {
  printf("Error type %d at Line %d: %s.\n", type, line, msg);
}

static Type *new_type(enum TypeKind kind) {
  Type *type = (Type *)xmalloc(sizeof(Type));
  type->kind = kind;
  return type;
}

static Type *new_basic_type(int basic) {
  Type *type = new_type(TYPE_BASIC);
  type->u.basic = basic;
  return type;
}

static Type *new_array_type(Type *elem, int size) {
  Type *type = new_type(TYPE_ARRAY);
  type->u.array.elem = elem;
  type->u.array.size = size;
  return type;
}

static Type *new_struct_type(const char *name, Field *fields) {
  Type *type = new_type(TYPE_STRUCT);
  type->u.structure.name = name ? xstrdup(name) : NULL;
  type->u.structure.fields = fields;
  return type;
}

static Field *new_field(const char *name, Type *type, int line) {
  Field *field = (Field *)xmalloc(sizeof(Field));
  field->name = xstrdup(name);
  field->type = type;
  field->line = line;
  return field;
}

static int is_error_type(Type *type) {
  return !type || type->kind == TYPE_ERROR;
}

static int is_int_type(Type *type) {
  return type && type->kind == TYPE_BASIC && type->u.basic == BASIC_INT;
}

static int is_numeric_type(Type *type) {
  return type && type->kind == TYPE_BASIC;
}

static int type_equal(Type *a, Type *b) {
  if (is_error_type(a) || is_error_type(b)) {
    return 1;
  }
  if (a->kind != b->kind) {
    return 0;
  }
  switch (a->kind) {
  case TYPE_BASIC:
    return a->u.basic == b->u.basic;
  case TYPE_ARRAY:
    return type_equal(a->u.array.elem, b->u.array.elem);
  case TYPE_STRUCT: {
    Field *fa = a->u.structure.fields;
    Field *fb = b->u.structure.fields;
    while (fa && fb) {
      if (!type_equal(fa->type, fb->type)) {
        return 0;
      }
      fa = fa->next;
      fb = fb->next;
    }
    return fa == NULL && fb == NULL;
  }
  case TYPE_ERROR:
    return 1;
  }
  return 0;
}

static int fields_equal(Field *a, Field *b) {
  while (a && b) {
    if (!type_equal(a->type, b->type)) {
      return 0;
    }
    a = a->next;
    b = b->next;
  }
  return a == NULL && b == NULL;
}

static int field_count(Field *fields) {
  int count = 0;
  for (; fields; fields = fields->next) {
    ++count;
  }
  return count;
}

static int name_seen(NameItem *list, const char *name) {
  for (; list; list = list->next) {
    if (strcmp(list->name, name) == 0) {
      return 1;
    }
  }
  return 0;
}

static void remember_var_name(const char *name) {
  if (name_seen(seen_vars, name)) {
    return;
  }
  NameItem *item = (NameItem *)xmalloc(sizeof(NameItem));
  item->name = xstrdup(name);
  item->next = seen_vars;
  seen_vars = item;
}

static StructSymbol *find_struct(const char *name) {
  for (StructSymbol *cur = structs; cur; cur = cur->next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

static void add_struct_symbol(const char *name, Type *type, int line) {
  StructSymbol *sym = (StructSymbol *)xmalloc(sizeof(StructSymbol));
  sym->name = xstrdup(name);
  sym->type = type;
  sym->line = line;
  sym->next = structs;
  structs = sym;
}

static void push_scope(void) {
  Scope *scope = (Scope *)xmalloc(sizeof(Scope));
  scope->depth = ++current_depth;
  scope->prev = current_scope;
  current_scope = scope;
}

static void pop_scope(void) {
  if (!current_scope) {
    return;
  }
  VarSymbol *sym = current_scope->symbols;
  while (sym) {
    VarSymbol *next = sym->scope_next;
    unsigned h = hash_pjw(sym->name);
    VarSymbol **slot = &var_table[h];
    while (*slot && *slot != sym) {
      slot = &(*slot)->bucket_next;
    }
    if (*slot) {
      *slot = sym->bucket_next;
    }
    sym = next;
  }
  Scope *old = current_scope;
  current_scope = current_scope->prev;
  free(old);
  --current_depth;
}

static VarSymbol *lookup_var_current(const char *name) {
  unsigned h = hash_pjw(name);
  for (VarSymbol *cur = var_table[h]; cur; cur = cur->bucket_next) {
    if (cur->depth == current_depth && strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

static VarSymbol *lookup_var(const char *name) {
  unsigned h = hash_pjw(name);
  for (VarSymbol *cur = var_table[h]; cur; cur = cur->bucket_next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

static int insert_var(const char *name, Type *type, int line) {
  char buf[160];
  if (find_struct(name)) {
    snprintf(buf, sizeof(buf), "Redefined variable \"%s\"", name);
    semantic_error(3, line, buf);
    return 0;
  }
  if (lookup_var_current(name)) {
    snprintf(buf, sizeof(buf), "Redefined variable \"%s\"", name);
    semantic_error(3, line, buf);
    return 0;
  }
  if (!current_scope) {
    push_scope();
  }
  VarSymbol *sym = (VarSymbol *)xmalloc(sizeof(VarSymbol));
  sym->name = xstrdup(name);
  sym->type = type;
  sym->depth = current_depth;
  unsigned h = hash_pjw(name);
  sym->bucket_next = var_table[h];
  var_table[h] = sym;
  sym->scope_next = current_scope->symbols;
  current_scope->symbols = sym;
  remember_var_name(name);
  return 1;
}

static Function *find_function(const char *name) {
  for (Function *cur = functions; cur; cur = cur->next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

static Function *add_function(const char *name, Type *ret, Field *params,
                              int defined, int line) {
  Function *func = (Function *)xmalloc(sizeof(Function));
  func->name = xstrdup(name);
  func->ret = ret;
  func->params = params;
  func->param_count = field_count(params);
  func->defined = defined;
  func->decl_line = line;
  func->next = functions;
  functions = func;
  return func;
}

static int same_signature(Function *func, Type *ret, Field *params) {
  return type_equal(func->ret, ret) && func->param_count == field_count(params) &&
         fields_equal(func->params, params);
}

static Field *find_field(Field *fields, const char *name) {
  for (; fields; fields = fields->next) {
    if (strcmp(fields->name, name) == 0) {
      return fields;
    }
  }
  return NULL;
}

static void append_field(Field **head, Field **tail, Field *field) {
  if (!*head) {
    *head = field;
  } else {
    (*tail)->next = field;
  }
  *tail = field;
}

static Type *analyze_specifier(TreeNode *node);
static Field *analyze_def_list_as_fields(TreeNode *node);
static void analyze_ext_def_list(TreeNode *node);
static void analyze_stmt_list(TreeNode *node, Type *ret_type);
static void analyze_def_list(TreeNode *node);
static ExprInfo analyze_exp(TreeNode *node);

static void collect_var_dec(TreeNode *node, char **name, int *line,
                            int *sizes, int *count) {
  TreeNode *first = child_at(node, 0);
  if (is_node(first, "ID")) {
    *name = first->text;
    *line = first->line;
    return;
  }
  collect_var_dec(first, name, line, sizes, count);
  TreeNode *size_node = child_at(node, 2);
  sizes[(*count)++] = size_node ? size_node->int_val : 0;
}

static Type *var_dec_type(TreeNode *node, Type *base, char **name, int *line) {
  int sizes[64];
  int count = 0;
  *name = NULL;
  *line = node ? node->line : 0;
  collect_var_dec(node, name, line, sizes, &count);
  Type *type = base;
  for (int i = count - 1; i >= 0; --i) {
    type = new_array_type(type, sizes[i]);
  }
  return type;
}

static Field *analyze_param_dec(TreeNode *node, int insert) {
  Type *base = analyze_specifier(child_at(node, 0));
  char *name = NULL;
  int line = node ? node->line : 0;
  Type *type = var_dec_type(child_at(node, 1), base, &name, &line);
  Field *field = new_field(name ? name : "", type, line);
  if (insert && name) {
    insert_var(name, type, line);
  }
  return field;
}

static Field *analyze_var_list(TreeNode *node, int insert) {
  Field *head = NULL;
  Field *tail = NULL;
  for (TreeNode *cur = node; cur;) {
    TreeNode *param = child_at(cur, 0);
    append_field(&head, &tail, analyze_param_dec(param, insert));
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
  return head;
}

static Field *function_params(TreeNode *fun_dec, int insert) {
  if (child_count(fun_dec) == 4) {
    return analyze_var_list(child_at(fun_dec, 2), insert);
  }
  return NULL;
}

static void insert_param_fields(Field *params) {
  for (Field *param = params; param; param = param->next) {
    insert_var(param->name, param->type, param->line);
  }
}

static const char *function_name(TreeNode *fun_dec) {
  TreeNode *id = child_at(fun_dec, 0);
  return id ? id->text : "";
}

static int function_line(TreeNode *fun_dec) {
  TreeNode *id = child_at(fun_dec, 0);
  return id ? id->line : (fun_dec ? fun_dec->line : 0);
}

static void register_function(TreeNode *fun_dec, Type *ret, Field *params,
                              int defined) {
  const char *name = function_name(fun_dec);
  int line = function_line(fun_dec);
  char buf[160];
  Function *func = find_function(name);
  if (!func) {
    add_function(name, ret, params, defined, line);
    return;
  }
  if (defined && func->defined) {
    snprintf(buf, sizeof(buf), "Redefined function \"%s\"", name);
    semantic_error(4, line, buf);
    return;
  }
  if (!same_signature(func, ret, params)) {
    snprintf(buf, sizeof(buf), "Inconsistent declaration of function \"%s\"", name);
    semantic_error(19, line, buf);
  }
  if (defined) {
    func->defined = 1;
  }
}

static Type *analyze_struct_specifier(TreeNode *node) {
  TreeNode *second = child_at(node, 1);
  char buf[160];
  if (is_node(second, "Tag")) {
    TreeNode *id = child_at(second, 0);
    StructSymbol *sym = id ? find_struct(id->text) : NULL;
    if (!sym) {
      snprintf(buf, sizeof(buf), "Undefined structure \"%s\"", id ? id->text : "");
      semantic_error(17, id ? id->line : node->line, buf);
      return error_type;
    }
    return sym->type;
  }

  const char *name = NULL;
  int line = node ? node->line : 0;
  TreeNode *def_list = find_child(node, "DefList");
  if (is_node(second, "OptTag")) {
    TreeNode *id = child_at(second, 0);
    name = id ? id->text : NULL;
    line = id ? id->line : line;
  }

  Field *fields = analyze_def_list_as_fields(def_list);
  Type *type = new_struct_type(name, fields);
  if (name) {
    if (find_struct(name) || name_seen(seen_vars, name) || lookup_var(name)) {
      snprintf(buf, sizeof(buf), "Duplicated name \"%s\"", name);
      semantic_error(16, line, buf);
    } else {
      add_struct_symbol(name, type, line);
    }
  }
  return type;
}

static Type *analyze_specifier(TreeNode *node) {
  TreeNode *first = child_at(node, 0);
  if (is_node(first, "TYPE")) {
    return strcmp(first->text, "int") == 0 ? int_type : float_type;
  }
  if (is_node(first, "StructSpecifier")) {
    return analyze_struct_specifier(first);
  }
  return error_type;
}

static Field *analyze_dec_list_as_fields(TreeNode *node, Type *base) {
  Field *head = NULL;
  Field *tail = NULL;
  for (TreeNode *cur = node; cur;) {
    TreeNode *dec = child_at(cur, 0);
    TreeNode *var_dec = child_at(dec, 0);
    char *name = NULL;
    int line = dec ? dec->line : 0;
    Type *type = var_dec_type(var_dec, base, &name, &line);
    if (child_count(dec) == 3) {
      char buf[160];
      snprintf(buf, sizeof(buf), "Initialized field \"%s\"", name ? name : "");
      semantic_error(15, line, buf);
    }
    if (name && find_field(head, name)) {
      char buf[160];
      snprintf(buf, sizeof(buf), "Redefined field \"%s\"", name);
      semantic_error(15, line, buf);
    } else if (name) {
      append_field(&head, &tail, new_field(name, type, line));
    }
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
  return head;
}

static Field *analyze_def_list_as_fields(TreeNode *node) {
  Field *head = NULL;
  Field *tail = NULL;
  for (TreeNode *cur = node; cur;) {
    TreeNode *def = child_at(cur, 0);
    Type *base = analyze_specifier(child_at(def, 0));
    Field *fields = analyze_dec_list_as_fields(child_at(def, 1), base);
    for (Field *f = fields; f; f = f->next) {
      if (find_field(head, f->name)) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Redefined field \"%s\"", f->name);
        semantic_error(15, f->line, buf);
      } else {
        append_field(&head, &tail, new_field(f->name, f->type, f->line));
      }
    }
    cur = child_at(cur, 1);
  }
  return head;
}

static void analyze_dec(TreeNode *dec, Type *base) {
  char *name = NULL;
  int line = dec ? dec->line : 0;
  Type *type = var_dec_type(child_at(dec, 0), base, &name, &line);
  if (name) {
    insert_var(name, type, line);
  }
  if (child_count(dec) == 3) {
    ExprInfo rhs = analyze_exp(child_at(dec, 2));
    if (!type_equal(type, rhs.type)) {
      semantic_error(5, line, "Type mismatched for assignment");
    }
  }
}

static void analyze_dec_list(TreeNode *node, Type *base) {
  for (TreeNode *cur = node; cur;) {
    analyze_dec(child_at(cur, 0), base);
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
}

static void analyze_def(TreeNode *node) {
  Type *base = analyze_specifier(child_at(node, 0));
  analyze_dec_list(child_at(node, 1), base);
}

static void analyze_def_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_def(child_at(cur, 0));
  }
}

static int is_assignable_syntax(TreeNode *node) {
  if (!is_node(node, "Exp")) {
    return 0;
  }
  int n = child_count(node);
  if (n == 1 && is_node(child_at(node, 0), "ID")) {
    return 1;
  }
  if (n == 4 && is_node(child_at(node, 1), "LB")) {
    return 1;
  }
  if (n == 3 && is_node(child_at(node, 1), "DOT")) {
    return 1;
  }
  return 0;
}

static ExprInfo make_expr(Type *type, int is_lvalue) {
  ExprInfo info;
  info.type = type;
  info.is_lvalue = is_lvalue;
  return info;
}

static Field *analyze_args(TreeNode *node) {
  Field *head = NULL;
  Field *tail = NULL;
  for (TreeNode *cur = node; cur;) {
    ExprInfo arg = analyze_exp(child_at(cur, 0));
    append_field(&head, &tail, new_field("", arg.type, child_at(cur, 0)->line));
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
  return head;
}

static ExprInfo analyze_call(TreeNode *node) {
  TreeNode *id = child_at(node, 0);
  Field *args = NULL;
  if (child_count(node) == 4) {
    args = analyze_args(child_at(node, 2));
  }
  char buf[200];
  Function *func = id ? find_function(id->text) : NULL;
  if (!func) {
    if (id && lookup_var(id->text)) {
      snprintf(buf, sizeof(buf), "\"%s\" is not a function", id->text);
      semantic_error(11, id->line, buf);
    } else {
      snprintf(buf, sizeof(buf), "Undefined function \"%s\"", id ? id->text : "");
      semantic_error(2, id ? id->line : node->line, buf);
    }
    return make_expr(error_type, 0);
  }
  if (func->param_count != field_count(args) || !fields_equal(func->params, args)) {
    snprintf(buf, sizeof(buf), "Function \"%s\" is not applicable for arguments",
             id ? id->text : "");
    semantic_error(9, id ? id->line : node->line, buf);
  }
  return make_expr(func->ret, 0);
}

static ExprInfo analyze_binary(TreeNode *node, const char *op_name) {
  ExprInfo lhs = analyze_exp(child_at(node, 0));
  ExprInfo rhs = analyze_exp(child_at(node, 2));
  if (is_error_type(lhs.type) || is_error_type(rhs.type)) {
    return make_expr(error_type, 0);
  }
  if (strcmp(op_name, "AND") == 0 || strcmp(op_name, "OR") == 0) {
    if (!is_int_type(lhs.type) || !is_int_type(rhs.type)) {
      semantic_error(7, node->line, "Type mismatched for operands");
      return make_expr(error_type, 0);
    }
    return make_expr(int_type, 0);
  }
  if (strcmp(op_name, "RELOP") == 0) {
    if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type) ||
        !type_equal(lhs.type, rhs.type)) {
      semantic_error(7, node->line, "Type mismatched for operands");
      return make_expr(error_type, 0);
    }
    return make_expr(int_type, 0);
  }
  if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type) ||
      !type_equal(lhs.type, rhs.type)) {
    semantic_error(7, node->line, "Type mismatched for operands");
    return make_expr(error_type, 0);
  }
  return make_expr(lhs.type, 0);
}

static ExprInfo analyze_exp(TreeNode *node) {
  if (!node) {
    return make_expr(error_type, 0);
  }
  int n = child_count(node);
  TreeNode *c0 = child_at(node, 0);
  TreeNode *c1 = child_at(node, 1);
  TreeNode *c2 = child_at(node, 2);
  char buf[200];

  if (n == 1) {
    if (is_node(c0, "ID")) {
      VarSymbol *var = lookup_var(c0->text);
      if (!var) {
        snprintf(buf, sizeof(buf), "Undefined variable \"%s\"", c0->text);
        semantic_error(1, c0->line, buf);
        return make_expr(error_type, 0);
      }
      return make_expr(var->type, 1);
    }
    if (is_node(c0, "INT")) {
      return make_expr(int_type, 0);
    }
    if (is_node(c0, "FLOAT")) {
      return make_expr(float_type, 0);
    }
  }

  if (n == 2) {
    ExprInfo operand = analyze_exp(c1);
    if (is_error_type(operand.type)) {
      return make_expr(error_type, 0);
    }
    if (is_node(c0, "NOT")) {
      if (!is_int_type(operand.type)) {
        semantic_error(7, node->line, "Type mismatched for operands");
        return make_expr(error_type, 0);
      }
      return make_expr(int_type, 0);
    }
    if (is_node(c0, "MINUS")) {
      if (!is_numeric_type(operand.type)) {
        semantic_error(7, node->line, "Type mismatched for operands");
        return make_expr(error_type, 0);
      }
      return make_expr(operand.type, 0);
    }
  }

  if (n == 3 && is_node(c0, "LP")) {
    ExprInfo inner = analyze_exp(c1);
    return make_expr(inner.type, 0);
  }

  if ((n == 3 || n == 4) && is_node(c0, "ID") && is_node(c1, "LP")) {
    return analyze_call(node);
  }

  if (n == 3 && is_node(c1, "ASSIGNOP")) {
    ExprInfo lhs = analyze_exp(c0);
    ExprInfo rhs = analyze_exp(c2);
    if (is_error_type(lhs.type)) {
      return make_expr(error_type, 0);
    }
    if (!is_assignable_syntax(c0) || !lhs.is_lvalue) {
      semantic_error(6, node->line,
                     "The left-hand side of an assignment must be a variable");
    } else if (!type_equal(lhs.type, rhs.type)) {
      semantic_error(5, node->line, "Type mismatched for assignment");
    }
    return make_expr(lhs.type, 0);
  }

  if (n == 3 && (is_node(c1, "PLUS") || is_node(c1, "MINUS") ||
                 is_node(c1, "STAR") || is_node(c1, "DIV") ||
                 is_node(c1, "RELOP") || is_node(c1, "AND") ||
                 is_node(c1, "OR"))) {
    return analyze_binary(node, c1->name);
  }

  if (n == 4 && is_node(c1, "LB")) {
    ExprInfo base = analyze_exp(c0);
    ExprInfo index = analyze_exp(c2);
    if (!is_error_type(index.type) && !is_int_type(index.type)) {
      semantic_error(12, c2 ? c2->line : node->line, "Array index is not an integer");
    }
    if (is_error_type(base.type)) {
      return make_expr(error_type, 0);
    }
    if (base.type->kind != TYPE_ARRAY) {
      semantic_error(10, node->line, "Not an array");
      return make_expr(error_type, 0);
    }
    return make_expr(base.type->u.array.elem, 1);
  }

  if (n == 3 && is_node(c1, "DOT")) {
    ExprInfo base = analyze_exp(c0);
    if (is_error_type(base.type)) {
      return make_expr(error_type, 0);
    }
    if (base.type->kind != TYPE_STRUCT) {
      semantic_error(13, node->line, "Illegal use of \".\"");
      return make_expr(error_type, 0);
    }
    Field *field = find_field(base.type->u.structure.fields, c2 ? c2->text : "");
    if (!field) {
      snprintf(buf, sizeof(buf), "Non-existent field \"%s\"", c2 ? c2->text : "");
      semantic_error(14, c2 ? c2->line : node->line, buf);
      return make_expr(error_type, 0);
    }
    return make_expr(field->type, 1);
  }

  return make_expr(error_type, 0);
}

static void analyze_comp_st(TreeNode *node, Type *ret_type, int create_scope) {
  if (create_scope) {
    push_scope();
  }
  analyze_def_list(find_child(node, "DefList"));
  analyze_stmt_list(find_child(node, "StmtList"), ret_type);
  if (create_scope) {
    pop_scope();
  }
}

static void analyze_stmt(TreeNode *node, Type *ret_type) {
  if (!node) {
    return;
  }
  TreeNode *first = child_at(node, 0);
  int n = child_count(node);
  if (is_node(first, "Exp")) {
    analyze_exp(first);
  } else if (is_node(first, "RETURN")) {
    ExprInfo ret = analyze_exp(child_at(node, 1));
    if (!type_equal(ret_type, ret.type)) {
      semantic_error(8, node->line, "Type mismatched for return");
    }
  } else if (is_node(first, "CompSt")) {
    analyze_comp_st(first, ret_type, 1);
  } else if (is_node(first, "IF")) {
    ExprInfo cond = analyze_exp(child_at(node, 2));
    if (!is_error_type(cond.type) && !is_int_type(cond.type)) {
      semantic_error(7, child_at(node, 2)->line, "Type mismatched for operands");
    }
    analyze_stmt(child_at(node, 4), ret_type);
    if (n == 7) {
      analyze_stmt(child_at(node, 6), ret_type);
    }
  } else if (is_node(first, "WHILE")) {
    ExprInfo cond = analyze_exp(child_at(node, 2));
    if (!is_error_type(cond.type) && !is_int_type(cond.type)) {
      semantic_error(7, child_at(node, 2)->line, "Type mismatched for operands");
    }
    analyze_stmt(child_at(node, 4), ret_type);
  }
}

static void analyze_stmt_list(TreeNode *node, Type *ret_type) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_stmt(child_at(cur, 0), ret_type);
  }
}

static void analyze_ext_dec_list(TreeNode *node, Type *base) {
  for (TreeNode *cur = node; cur;) {
    char *name = NULL;
    int line = cur ? cur->line : 0;
    Type *type = var_dec_type(child_at(cur, 0), base, &name, &line);
    if (name) {
      insert_var(name, type, line);
    }
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
}

static void analyze_ext_def(TreeNode *node) {
  if (!node) {
    return;
  }
  Type *base = analyze_specifier(child_at(node, 0));
  TreeNode *second = child_at(node, 1);
  TreeNode *third = child_at(node, 2);
  if (is_node(second, "ExtDecList")) {
    analyze_ext_dec_list(second, base);
  } else if (is_node(second, "FunDec")) {
    int is_definition = is_node(third, "CompSt");
    Field *params = function_params(second, 0);
    register_function(second, base, params, is_definition);
    if (is_definition) {
      push_scope();
      insert_param_fields(params);
      analyze_comp_st(third, base, 0);
      pop_scope();
    }
  }
}

static void analyze_ext_def_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_ext_def(child_at(cur, 0));
  }
}

static void check_undefined_functions(void) {
  char buf[160];
  for (Function *func = functions; func; func = func->next) {
    if (!func->defined) {
      snprintf(buf, sizeof(buf), "Undefined function \"%s\"", func->name);
      semantic_error(18, func->decl_line, buf);
    }
  }
}

void semantic_analyze(TreeNode *root) {
  memset(var_table, 0, sizeof(var_table));
  current_scope = NULL;
  current_depth = 0;
  functions = NULL;
  structs = NULL;
  seen_vars = NULL;
  int_type = new_basic_type(BASIC_INT);
  float_type = new_basic_type(BASIC_FLOAT);
  error_type = new_type(TYPE_ERROR);

  push_scope();
  analyze_ext_def_list(child_at(root, 0));
  check_undefined_functions();
  pop_scope();
}

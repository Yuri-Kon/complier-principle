#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * semantic.c 是实验二的核心模块。
 *
 * Bison 只负责构造语法树；这里再对语法树做一遍自顶向下/递归遍历，
 * 完成三类工作：
 * 1. 根据 Specifier / VarDec / FunDec / StructSpecifier 构造类型信息；
 * 2. 维护变量、函数、结构体三类符号表；
 * 3. 在表达式和语句处检查实验要求的语义错误并输出 Error type 1-19。
 *
 * 为了支持选做 2.2 的嵌套作用域，变量符号表采用“一个哈希表 + 作用域链表”
 * 的 imperative style：进入作用域时新建 Scope，离开作用域时删除本层变量。
 */
#define HASH_SIZE 16384
// 定义哈希桶数量

typedef struct Type Type;
typedef struct Field Field;
typedef struct VarSymbol VarSymbol;
typedef struct Scope Scope;
typedef struct Function Function;
typedef struct StructSymbol StructSymbol;
typedef struct NameItem NameItem;

enum TypeKind { TYPE_BASIC, TYPE_ARRAY, TYPE_STRUCT, TYPE_ERROR };
enum BasicKind { BASIC_INT, BASIC_FLOAT };

/* Type 描述 C-- 中所有可检查的类型：基本类型、数组、结构体和错误哨兵。 */
struct Type {
  enum TypeKind kind;
  union {
    /* BASIC: basic 为 BASIC_INT 或 BASIC_FLOAT。 */
    int basic;
    /* ARRAY: elem 指向元素类型；size 记录当前维度长度，类型等价时不比较长度。
     */
    struct {
      Type *elem;
      int size;
    } array;
    /* STRUCT: name 可为空，匿名结构体也保留字段链表用于结构等价。 */
    struct {
      char *name;
      Field *fields;
    } structure;
  } u;
};

/* Field 同时用于结构体字段链表、函数形参链表和实参类型链表。 */
struct Field {
  char *name;
  Type *type;
  int line;
  Field *next;
};

/*
 * VarSymbol 是变量符号。
 * bucket_next 串起哈希冲突链；scope_next 串起同一作用域内定义的变量，
 * 这样 pop_scope 时可以快速找到并删除当前层的所有变量。
 */
struct VarSymbol {
  char *name;
  Type *type;
  int depth;
  VarSymbol *bucket_next;
  VarSymbol *scope_next;
};

/* Scope 记录一层语句块作用域。depth 用于区分同名变量所在层级。 */
struct Scope {
  int depth;
  VarSymbol *symbols;
  Scope *prev;
};

/* Function 记录函数声明/定义的签名，并支持错误 18、19 的检查。 */
struct Function {
  char *name;
  Type *ret;
  Field *params;
  int param_count; // 检查函数是否多次声明或是否与定义一致
  int defined;     // 表示函数是否有定义
  int decl_line;
  Function *next;
};

/* 结构体名表。结构体变量的字段类型保存在 type 中。
 * 结构体是全局命名空间，所以用普通链表即可
 */
struct StructSymbol {
  char *name;
  Type *type;
  int line;
  StructSymbol *next;
};

/*
 * seen_vars 只记录“曾经定义过”的变量名。
 * 选做 2.2 中局部变量离开作用域会从 var_table 删除，但结构体名仍不能和
 * 之前出现过的变量名重复，因此需要额外保留历史变量名。
 */
struct NameItem {
  char *name;
  NameItem *next;
};

/* 表达式分析结果：除了类型，还要知道它是否能作为赋值左值。 */
typedef struct {
  Type *type;
  int is_lvalue;
} ExprInfo;

/* 全局分析状态。每次 semantic_analyze 开始时都会重新初始化。 */
static VarSymbol *var_table[HASH_SIZE];
static Scope *current_scope = NULL;
static int current_depth = 0;
static Function *functions = NULL;
static StructSymbol *structs = NULL;
static NameItem *seen_vars = NULL;
static Type *int_type = NULL;
static Type *float_type = NULL;
static Type *error_type = NULL;
/* semantic_errors 用于实验三：语义正确时才允许进入中间代码生成。 */
static int semantic_errors = 0;

/* 带错误检查的内存分配，统一清零，减少字段遗漏初始化的问题。 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  memset(p, 0, size);
  return p;
}

/* strdup 不是 C 标准库函数的一部分，这里自己实现。 */
static char *xstrdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *p = (char *)xmalloc(len);
  memcpy(p, s, len);
  return p;
}

/* 教程推荐的 PJW 哈希，用于变量表的桶定位。 */
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

/* 以下几个小函数封装语法树访问，避免到处手写 child/sibling 遍历。 */
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

/* 统一语义错误输出格式。说明文字可以不同，但类型号和行号必须正确。 */
static void semantic_error(int type, int line, const char *msg) {
  ++semantic_errors;
  printf("Error type %d at Line %d: %s.\n", type, line, msg);
}

/* 类型和字段构造函数。语义分析阶段不修改已有 Type，通常直接复用指针。 */
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

/* TYPE_ERROR 是内部哨兵：已经报过根因错误时向上传递，避免连锁误报。 */
static int is_error_type(Type *type) {
  return !type || type->kind == TYPE_ERROR;
}

/* 类型分类判断，用于逻辑运算、算术运算、数组下标等规则。 */
static int is_int_type(Type *type) {
  return type && type->kind == TYPE_BASIC && type->u.basic == BASIC_INT;
}

static int is_numeric_type(Type *type) {
  return type && type->kind == TYPE_BASIC;
}

/*
 * 类型等价：
 * - int 与 float 必须完全一致；
 * - 数组只比较元素类型，等价于“基类型和维数相同”，忽略每维长度；
 * - 结构体实现选做 2.3 的结构等价，按字段顺序逐个比较字段类型。
 */
static int type_equal(Type *a, Type *b) {
  if (is_error_type(a) || is_error_type(b)) {
    return 1;
  }
  if (a->kind != b->kind) {
    return 0;
  }
  switch (a->kind) {
  case TYPE_BASIC:
    // Basic 分支只有两个，int 和 float, 这里比较是符合假设的
    return a->u.basic == b->u.basic;
  case TYPE_ARRAY:
    // Array 类型只比较 elem, 数组元素类型
    // 这里是递归比较
    return type_equal(a->u.array.elem, b->u.array.elem);
  case TYPE_STRUCT: {
    // 比较字段数量和字段类型的顺序
    // 这里是结构等价
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

/*
 * 用于比较函数参数列表或实参列表。
 * 逐个比较字段类型，不关心字段名
 */
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

/* 安全追加字符串到固定缓冲区，用于拼接错误信息。 */
static void append_text(char *buf, size_t size, const char *text) {
  size_t used = strlen(buf);
  if (used < size - 1) {
    snprintf(buf + used, size - used, "%s", text);
  }
}

/*
 * 将内部 Type 转成可读文本，主要用于错误 9 的函数签名。
 * 例如 int、float、int[]、struct Position。
 */
static void type_to_string(Type *type, char *buf, size_t size) {
  if (!type) {
    append_text(buf, size, "<error>");
    return;
  }
  switch (type->kind) {
  case TYPE_BASIC:
    append_text(buf, size, type->u.basic == BASIC_INT ? "int" : "float");
    break;
  case TYPE_ARRAY:
    type_to_string(type->u.array.elem, buf, size);
    append_text(buf, size, "[]");
    break;
  case TYPE_STRUCT:
    append_text(buf, size, "struct");
    if (type->u.structure.name) {
      append_text(buf, size, " ");
      append_text(buf, size, type->u.structure.name);
    }
    break;
  case TYPE_ERROR:
    append_text(buf, size, "<error>");
    break;
  }
}

/* 将形参/实参链表格式化成 "(int, float)" 这样的形式。 */
static void field_list_to_string(Field *fields, char *buf, size_t size) {
  append_text(buf, size, "(");
  for (Field *cur = fields; cur; cur = cur->next) {
    if (cur != fields) {
      append_text(buf, size, ", ");
    }
    type_to_string(cur->type, buf, size);
  }
  append_text(buf, size, ")");
}

/* 将函数名和形参列表格式化成 "func(int)"。 */
static void function_signature_to_string(Function *func, char *buf,
                                         size_t size) {
  append_text(buf, size, func->name);
  field_list_to_string(func->params, buf, size);
}

/*
 * 数组访问报错时，实验样例希望输出变量名，例如 "i" is not an array。
 * 这里从表达式子树中取第一个 ID，能覆盖 i[0] 以及更复杂的嵌套访问。
 */
static TreeNode *first_id_in_exp(TreeNode *node) {
  if (!node) {
    return NULL;
  }
  if (is_node(node, "ID")) {
    return node;
  }
  for (TreeNode *cur = node->child; cur; cur = cur->sibling) {
    TreeNode *found = first_id_in_exp(cur);
    if (found) {
      return found;
    }
  }
  return NULL;
}

/*
 * 将简单表达式转成文本，用于错误 12 的下标提示。
 * 当前只需要支持 ID / INT / FLOAT；复杂表达式用 <expr> 兜底。
 */
static void exp_to_string(TreeNode *node, char *buf, size_t size) {
  if (!node) {
    append_text(buf, size, "<expr>");
    return;
  }
  if (is_node(node, "Exp") && child_count(node) == 1) {
    exp_to_string(child_at(node, 0), buf, size);
    return;
  }
  if (is_node(node, "ID") || is_node(node, "TYPE")) {
    append_text(buf, size, node->text);
  } else if (is_node(node, "INT")) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", node->int_val);
    append_text(buf, size, tmp);
  } else if (is_node(node, "FLOAT")) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", node->float_val);
    append_text(buf, size, tmp);
  } else {
    append_text(buf, size, "<expr>");
  }
}

/* 计算字段链表长度，函数签名比较和实参数目检查都会用到。 */
static int field_count(Field *fields) {
  int count = 0;
  for (; fields; fields = fields->next) {
    ++count;
  }
  return count;
}

/* seen_vars 是普通链表，规模很小，线性查找足够。 */
static int name_seen(NameItem *list, const char *name) {
  for (; list; list = list->next) {
    if (strcmp(list->name, name) == 0) {
      return 1;
    }
  }
  return 0;
}

/* 记录历史变量名，用于结构体名与曾经出现过的变量名冲突检查。 */
static void remember_var_name(const char *name) {
  if (name_seen(seen_vars, name)) {
    return;
  }
  NameItem *item = (NameItem *)xmalloc(sizeof(NameItem));
  item->name = xstrdup(name);
  item->next = seen_vars;
  seen_vars = item;
}

/* 查找/插入结构体名表。结构体名不受局部作用域影响。 */
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

/*
 * 进入一层新的变量作用域。
 * 全局作用域、函数体作用域和嵌套 CompSt 都使用同一套机制。
 */
static void push_scope(void) {
  Scope *scope = (Scope *)xmalloc(sizeof(Scope));
  scope->depth = ++current_depth;
  scope->prev = current_scope;
  current_scope = scope;
}

/*
 * 离开作用域时，只删除本层定义的变量。
 * 删除过程同时维护哈希桶链表和作用域内链表，外层同名变量会重新可见。
 */
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

/* 只查当前作用域，用于判断“同一层重复定义”。 */
static VarSymbol *lookup_var_current(const char *name) {
  unsigned h = hash_pjw(name);
  for (VarSymbol *cur = var_table[h]; cur; cur = cur->bucket_next) {
    if (cur->depth == current_depth && strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

/* 从哈希桶头开始查找变量；内层变量插在桶头，所以会优先命中最近定义。 */
static VarSymbol *lookup_var(const char *name) {
  unsigned h = hash_pjw(name);
  for (VarSymbol *cur = var_table[h]; cur; cur = cur->bucket_next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

/*
 * 插入变量并检查错误 3。
 * 选做 2.2 允许内层遮蔽外层，所以这里只禁止当前层重名；
 * 但变量名不能和已经定义的结构体名重复。
 */
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

/* 函数名表不支持嵌套定义，因此全局链表即可。 */
static Function *find_function(const char *name) {
  for (Function *cur = functions; cur; cur = cur->next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

/* 新增函数声明或定义。defined 标记它是否已经有函数体。 */
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

/* 判断声明和定义是否一致：返回类型、参数个数、参数类型都必须相同。 */
static int same_signature(Function *func, Type *ret, Field *params) {
  return type_equal(func->ret, ret) &&
         func->param_count == field_count(params) &&
         fields_equal(func->params, params);
}

/* 在结构体字段链表中按字段名查找。 */
static Field *find_field(Field *fields, const char *name) {
  for (; fields; fields = fields->next) {
    if (strcmp(fields->name, name) == 0) {
      return fields;
    }
  }
  return NULL;
}

/* 维护链表尾指针，按源码顺序追加字段或参数。 */
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

/*
 * VarDec 的语法是递归的：
 *   VarDec -> ID
 *   VarDec -> VarDec LB INT RB
 * 对 int a[10][2]，AST 里 ID 在最深处。本函数先找到 ID，再按出现顺序收集
 * 每一维大小，供 var_dec_type 从内到外构造 ARRAY 类型。
 */
static void collect_var_dec(TreeNode *node, char **name, int *line, int *sizes,
                            int *count) {
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

/* 从 VarDec 子树和基础类型构造变量完整类型，同时返回变量名和定义行号。 */
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

/*
 * 处理函数形参 ParamDec。
 * insert 为 0 时只构造签名；insert 为 1 时同时把形参作为变量放入函数作用域。
 */
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

/* 将 VarList 展开成 Field 链表，保持参数从左到右的顺序。 */
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

/* FunDec 有两种形态：ID LP RP 或 ID LP VarList RP。 */
static Field *function_params(TreeNode *fun_dec, int insert) {
  if (child_count(fun_dec) == 4) {
    return analyze_var_list(child_at(fun_dec, 2), insert);
  }
  return NULL;
}

/* 函数定义进入函数体前，把形参插入当前函数作用域。 */
static void insert_param_fields(Field *params) {
  for (Field *param = params; param; param = param->next) {
    insert_var(param->name, param->type, param->line);
  }
}

/* 读取 FunDec 中的函数名和定义行号，集中处理可减少调用处的 AST 细节。 */
static const char *function_name(TreeNode *fun_dec) {
  TreeNode *id = child_at(fun_dec, 0);
  return id ? id->text : "";
}

static int function_line(TreeNode *fun_dec) {
  TreeNode *id = child_at(fun_dec, 0);
  return id ? id->line : (fun_dec ? fun_dec->line : 0);
}

/*
 * 处理函数声明/定义：
 * - 第一次出现：加入函数表；
 * - 重复定义：错误 4；
 * - 多次声明或声明/定义不一致：错误 19；
 * - 仅声明未定义的情况在遍历结束后统一检查为错误 18。
 */
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
    snprintf(buf, sizeof(buf), "Inconsistent declaration of function \"%s\"",
             name);
    semantic_error(19, line, buf);
  }
  if (defined) {
    func->defined = 1;
  }
}

/*
 * 处理 StructSpecifier。
 * - STRUCT Tag：引用已有结构体，未找到时报错误 17；
 * - STRUCT OptTag LC DefList RC：定义新结构体，收集字段并检查错误 16。
 */
static Type *analyze_struct_specifier(TreeNode *node) {
  TreeNode *second = child_at(node, 1);
  char buf[160];
  if (is_node(second, "Tag")) {
    TreeNode *id = child_at(second, 0);
    StructSymbol *sym = id ? find_struct(id->text) : NULL;
    if (!sym) {
      snprintf(buf, sizeof(buf), "Undefined structure \"%s\"",
               id ? id->text : "");
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

/* Specifier 统一处理 TYPE 和 StructSpecifier，返回后续声明使用的基础类型。 */
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

/*
 * 结构体内部的 DecList 与普通变量定义不同：
 * - 字段不能初始化，初始化时报错误 15；
 * - 同一结构体内字段名不能重复，重复时报错误 15。
 */
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

/*
 * 将结构体 DefList 转换为字段链表。
 * 这里还会捕获跨 Def 的字段重名，例如 float x; int x;。
 */
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

/* 处理普通变量定义 Dec，并检查定义时初始化的赋值类型是否匹配。 */
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

/* DecList 是逗号连接的变量声明列表，逐个交给 analyze_dec。 */
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

/* Def = Specifier DecList SEMI，是局部变量定义的基本单元。 */
static void analyze_def(TreeNode *node) {
  Type *base = analyze_specifier(child_at(node, 0));
  analyze_dec_list(child_at(node, 1), base);
}

/* 局部定义列表可能为空；空指针会自然结束循环。 */
static void analyze_def_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_def(child_at(cur, 0));
  }
}

/*
 * 左值检查采用实验指导建议的语法层面规则：
 * 只有 ID、数组访问 Exp LB Exp RB、结构体字段访问 Exp DOT ID 能出现在赋值左边。
 */
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

/* 统一构造表达式分析结果。 */
static ExprInfo make_expr(Type *type, int is_lvalue) {
  ExprInfo info;
  info.type = type;
  info.is_lvalue = is_lvalue;
  return info;
}

/* 实参列表 Args 会被转换成只有类型有意义的 Field 链表，供函数调用匹配。 */
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

/*
 * 检查函数调用：
 * - 函数名未定义：错误 2；
 * - 对变量使用调用操作符：错误 11；
 * - 实参数目或类型不匹配：错误 9。
 */
static ExprInfo analyze_call(TreeNode *node) {
  TreeNode *id = child_at(node, 0);
  Field *args = NULL;
  if (child_count(node) == 4) {
    args = analyze_args(child_at(node, 2));
  }
  char buf[1024];
  Function *func = id ? find_function(id->text) : NULL;
  if (!func) {
    if (id && lookup_var(id->text)) {
      snprintf(buf, sizeof(buf), "\"%s\" is not a function", id->text);
      semantic_error(11, id->line, buf);
    } else {
      snprintf(buf, sizeof(buf), "Undefined function \"%s\"",
               id ? id->text : "");
      semantic_error(2, id ? id->line : node->line, buf);
    }
    return make_expr(error_type, 0);
  }
  if (func->param_count != field_count(args) ||
      !fields_equal(func->params, args)) {
    char signature[256] = "";
    char arg_text[256] = "";
    function_signature_to_string(func, signature, sizeof(signature));
    field_list_to_string(args, arg_text, sizeof(arg_text));
    snprintf(buf, sizeof(buf),
             "Function \"%s\" is not applicable for arguments \"%s\"",
             signature, arg_text);
    semantic_error(9, id ? id->line : node->line, buf);
  }
  return make_expr(func->ret, 0);
}

/*
 * 检查二元运算。
 * 逻辑运算要求 int；关系运算要求同类型 int/float 并返回 int；
 * 算术运算要求同类型 int/float，结果类型与操作数一致。
 */
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

/*
 * 表达式类型推导与错误检查的中心函数。
 * 每个分支对应 syntax.y 中 Exp 的一种产生式；返回 TYPE_ERROR 可阻止父节点
 * 因同一个根因继续产生连锁的类型错误。
 */
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
    /* ID / INT / FLOAT：变量使用检查错误 1，常量直接给出基本类型。 */
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
    /* 单目 NOT 和负号。NOT 只能作用于 int；负号只能作用于 int/float。 */
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
    /* 括号表达式不改变类型，也不是左值。 */
    ExprInfo inner = analyze_exp(c1);
    return make_expr(inner.type, 0);
  }

  if ((n == 3 || n == 4) && is_node(c0, "ID") && is_node(c1, "LP")) {
    /* 函数调用，包括无参调用和带参调用。 */
    return analyze_call(node);
  }

  if (n == 3 && is_node(c1, "ASSIGNOP")) {
    /* 赋值表达式：先检查左值合法性，再检查左右类型是否等价。 */
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

  if (n == 3 &&
      (is_node(c1, "PLUS") || is_node(c1, "MINUS") || is_node(c1, "STAR") ||
       is_node(c1, "DIV") || is_node(c1, "RELOP") || is_node(c1, "AND") ||
       is_node(c1, "OR"))) {
    return analyze_binary(node, c1->name);
  }

  if (n == 4 && is_node(c1, "LB")) {
    /* 数组访问：基表达式必须是数组，下标表达式必须是 int。 */
    ExprInfo base = analyze_exp(c0);
    ExprInfo index = analyze_exp(c2);
    if (!is_error_type(index.type) && !is_int_type(index.type)) {
      char index_text[128] = "";
      exp_to_string(c2, index_text, sizeof(index_text));
      snprintf(buf, sizeof(buf), "\"%s\" is not an integer", index_text);
      semantic_error(12, c2 ? c2->line : node->line, buf);
    }
    if (is_error_type(base.type)) {
      return make_expr(error_type, 0);
    }
    if (base.type->kind != TYPE_ARRAY) {
      TreeNode *id = first_id_in_exp(c0);
      snprintf(buf, sizeof(buf), "\"%s\" is not an array",
               id ? id->text : "<expr>");
      semantic_error(10, node->line, buf);
      return make_expr(error_type, 0);
    }
    return make_expr(base.type->u.array.elem, 1);
  }

  if (n == 3 && is_node(c1, "DOT")) {
    /* 结构体字段访问：基表达式必须是结构体，字段名必须存在。 */
    ExprInfo base = analyze_exp(c0);
    if (is_error_type(base.type)) {
      return make_expr(error_type, 0);
    }
    if (base.type->kind != TYPE_STRUCT) {
      semantic_error(13, node->line, "Illegal use of \".\"");
      return make_expr(error_type, 0);
    }
    Field *field =
        find_field(base.type->u.structure.fields, c2 ? c2->text : "");
    if (!field) {
      snprintf(buf, sizeof(buf), "Non-existent field \"%s\"",
               c2 ? c2->text : "");
      semantic_error(14, c2 ? c2->line : node->line, buf);
      return make_expr(error_type, 0);
    }
    return make_expr(field->type, 1);
  }

  return make_expr(error_type, 0);
}

/*
 * 处理复合语句 CompSt。
 * 函数体已经在外层创建过函数作用域，所以 create_scope=0；
 * 嵌套语句块需要创建新作用域，所以 create_scope=1。
 */
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

/*
 * 处理语句并携带当前函数的返回类型：
 * - return 检查错误 8；
 * - if/while 条件必须为 int；
 * - 子 CompSt 触发新的局部作用域。
 */
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
      semantic_error(7, child_at(node, 2)->line,
                     "Type mismatched for operands");
    }
    analyze_stmt(child_at(node, 4), ret_type);
    if (n == 7) {
      analyze_stmt(child_at(node, 6), ret_type);
    }
  } else if (is_node(first, "WHILE")) {
    ExprInfo cond = analyze_exp(child_at(node, 2));
    if (!is_error_type(cond.type) && !is_int_type(cond.type)) {
      semantic_error(7, child_at(node, 2)->line,
                     "Type mismatched for operands");
    }
    analyze_stmt(child_at(node, 4), ret_type);
  }
}

/* 语句列表是右递归链表，顺序遍历即可。 */
static void analyze_stmt_list(TreeNode *node, Type *ret_type) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_stmt(child_at(cur, 0), ret_type);
  }
}

/* 处理全局变量声明列表 ExtDecList。 */
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

/*
 * 顶层定义 ExtDef：
 * - Specifier ExtDecList SEMI：全局变量；
 * - Specifier FunDec SEMI：函数声明；
 * - Specifier FunDec CompSt：函数定义；
 * - Specifier SEMI：单独结构体定义或空声明，Specifier 已完成必要检查。
 */
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

/* 顶层定义列表。函数必须先注册签名，再分析函数体，便于函数体内调用已见函数。 */
static void analyze_ext_def_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    analyze_ext_def(child_at(cur, 0));
  }
}

/* 完整遍历结束后，所有只声明未定义的函数都在这里报错误 18。 */
static void check_undefined_functions(void) {
  char buf[160];
  for (Function *func = functions; func; func = func->next) {
    if (!func->defined) {
      snprintf(buf, sizeof(buf), "Undefined function \"%s\"", func->name);
      semantic_error(18, func->decl_line, buf);
    }
  }
}

/*
 * 实验三要求预定义 read/write：
 * - read(): int，对应 IR 的 READ；
 * - write(int): int，对应 IR 的 WRITE，返回值固定为 0。
 * 预先加入函数表后，语义分析可像普通函数一样检查调用是否合法。
 */
static void add_builtin_functions(void) {
  add_function("read", int_type, NULL, 1, 0);
  Field *write_params = new_field("x", int_type, 0);
  add_function("write", int_type, write_params, 1, 0);
}

/*
 * 语义分析入口。
 * 初始化所有全局状态和三个基础类型，然后创建全局作用域并遍历 Program。
 */
void semantic_analyze(TreeNode *root) {
  memset(var_table, 0, sizeof(var_table));
  current_scope = NULL;
  current_depth = 0;
  functions = NULL;
  structs = NULL;
  seen_vars = NULL;
  semantic_errors = 0;
  int_type = new_basic_type(BASIC_INT);
  float_type = new_basic_type(BASIC_FLOAT);
  error_type = new_type(TYPE_ERROR);
  add_builtin_functions();

  push_scope();
  analyze_ext_def_list(child_at(root, 0));
  check_undefined_functions();
  pop_scope();
}

int semantic_error_count(void) { return semantic_errors; }

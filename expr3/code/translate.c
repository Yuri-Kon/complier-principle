#include "translate.h"

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * translate.c 是实验三的代码生成模块。
 *
 * 设计取舍：
 * 1. 语义分析仍然由 semantic.c 负责，本模块只在语义正确的 AST 上工作；
 * 2. 为了计算数组元素宽度、结构体字段偏移、函数参数是否需要传引用，
 *    翻译阶段维护一套轻量级类型环境；
 * 3. 中间代码先通过 ir_emit 进入链表，最后统一写入输出文件；
 * 4. 结构体和数组在 IR 中按地址处理，基本类型按值处理。
 */
typedef struct Type Type;
typedef struct Field Field;
typedef struct VarSymbol VarSymbol;
typedef struct Scope Scope;
typedef struct Function Function;
typedef struct StructSymbol StructSymbol;
typedef struct ArgItem ArgItem;

enum TypeKind { TYPE_BASIC, TYPE_ARRAY, TYPE_STRUCT };
enum BasicKind { BASIC_INT, BASIC_FLOAT };

/*
 * 翻译阶段只需要类型的运行时布局信息：
 * - BASIC 固定宽度 4；
 * - ARRAY 记录当前维度长度和元素类型，用于计算 a[i] 的偏移；
 * - STRUCT 记录字段链表，字段里会写入从结构体首地址开始的字节偏移。
 */
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

/* Field 同时用于结构体字段和函数形参。offset 只对结构体字段有意义。 */
struct Field {
  char *name;
  Type *type;
  int offset;
  Field *next;
};

/*
 * by_ref 标记当前变量名在 IR 中保存的是值还是地址。
 * 数组/结构体形参按引用传递，PARAM x 接收到的 x 本身就是实参首地址；
 * 普通局部数组/结构体变量则需要用 &x 取得首地址。
 */
struct VarSymbol {
  char *name;
  Type *type;
  int by_ref;
  VarSymbol *next;
};

/* 翻译复合语句时维护作用域，保证内层同名变量查找优先级正确。 */
struct Scope {
  VarSymbol *vars;
  Scope *prev;
};

/* 函数签名用于函数调用返回类型判断，以及收集参数传递方式。 */
struct Function {
  char *name;
  Type *ret;
  Field *params;
  Function *next;
};

/* 结构体名表用于把 STRUCT Tag 解析成已计算好字段偏移的 Type。 */
struct StructSymbol {
  char *name;
  Type *type;
  StructSymbol *next;
};

/*
 * 实参临时链表。
 * 教程规定 ARG 输出顺序与源程序形参顺序相反，因此这里使用头插法保存实参。
 */
struct ArgItem {
  char place[64];
  ArgItem *next;
};

/* 全局翻译状态，每次 translate_program 开始时都会重新初始化。 */
static Type *int_type = NULL;
static Type *float_type = NULL;
static StructSymbol *structs = NULL;
static Function *functions = NULL;
static Scope *current_scope = NULL;
static int temp_no = 1;
static int label_no = 1;

/* 翻译器内部统一分配清零内存，减少结构字段漏初始化的风险。 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  memset(p, 0, size);
  return p;
}

/* 本文件不依赖非标准 strdup */
static char *xstrdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *p = (char *)xmalloc(len);
  memcpy(p, s, len);
  return p;
}

/* 以下 AST 辅助函数沿用实验二的长子-兄弟树访问方式。 */
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

/* 临时变量和标号均只需满足虚拟机变量名规范，不要求与样例完全一致。 */
static void new_temp(char *buf, size_t size) {
  snprintf(buf, size, "t%d", temp_no++);
}

static void new_label(char *buf, size_t size) {
  snprintf(buf, size, "label%d", label_no++);
}

/* 类型和字段构造函数。翻译阶段只构造，不释放，生命周期等同于一次翻译。 */
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

static Field *new_field(const char *name, Type *type) {
  Field *field = (Field *)xmalloc(sizeof(Field));
  field->name = xstrdup(name ? name : "");
  field->type = type;
  return field;
}

/*
 * 计算类型占用字节数：
 * - int/float 都按 4 字节；
 * - 数组大小为维度长度乘元素宽度；
 * - 结构体按字段顺序紧密排列，本实验不额外做对齐填充。
 */
static int type_size(Type *type) {
  if (!type) {
      // 这里是防御性返回。一般情况下 type 的类型不会出现 NULL ，
      // 这里是当出现这种情况时
      // 防御性返回基本宽类型以保证后续处理
    return 4;
  }
  switch (type->kind) {
  case TYPE_BASIC:
    return 4;
  case TYPE_ARRAY:
    return type->u.array.size * type_size(type->u.array.elem);
  case TYPE_STRUCT: {
    int size = 0;
    for (Field *field = type->u.structure.fields; field; field = field->next) {
      size += type_size(field->type);
    }
    return size;
  }
  }
  return 4;
}

/* 数组和结构体在赋值、传参和访问时需要按地址处理。 */
static int is_ref_type(Type *type) {
  return type && (type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT);
}

/* 当前实验结构体字段较少，链表线性查找足够。 */
static Field *find_field(Field *fields, const char *name) {
  for (Field *field = fields; field; field = field->next) {
    if (strcmp(field->name, name) == 0) {
      return field;
    }
  }
  return NULL;
}

/* 结构体名与函数名均处于全局翻译环境。 */
static StructSymbol *find_struct(const char *name) {
  for (StructSymbol *cur = structs; cur; cur = cur->next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

/* 匿名结构体不进入结构体名表，但仍可作为变量类型参与布局计算。 */
static void add_struct(const char *name, Type *type) {
  if (!name || find_struct(name)) {
    return;
  }
  StructSymbol *sym = (StructSymbol *)xmalloc(sizeof(StructSymbol));
  sym->name = xstrdup(name);
  sym->type = type;
  sym->next = structs;
  structs = sym;
}

/* 函数表在正式翻译函数体前预收集，支持函数体中调用后定义函数。 */
static Function *find_function(const char *name) {
  for (Function *cur = functions; cur; cur = cur->next) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
  }
  return NULL;
}

static void add_function(const char *name, Type *ret, Field *params) {
  if (find_function(name)) {
    return;
  }
  Function *func = (Function *)xmalloc(sizeof(Function));
  func->name = xstrdup(name);
  func->ret = ret;
  func->params = params;
  func->next = functions;
  functions = func;
}

/* 进入/离开 CompSt 时维护局部变量表；函数体本身在 translate_function 中建作用域。 */
static void push_scope(void) {
  Scope *scope = (Scope *)xmalloc(sizeof(Scope));
  scope->prev = current_scope;
  current_scope = scope;
}

/* 离开作用域时只释放变量符号节点，类型信息由整次翻译统一保留。 */
static void pop_scope(void) {
  Scope *scope = current_scope;
  if (!scope) {
    return;
  }
  VarSymbol *var = scope->vars;
  while (var) {
    VarSymbol *next = var->next;
    free(var);
    var = next;
  }
  current_scope = scope->prev;
  free(scope);
}

/* 从内层到外层查找变量，匹配实验二允许局部遮蔽的作用域规则。 */
static VarSymbol *find_var(const char *name) {
  for (Scope *scope = current_scope; scope; scope = scope->prev) {
    for (VarSymbol *var = scope->vars; var; var = var->next) {
      if (strcmp(var->name, name) == 0) {
        return var;
      }
    }
  }
  return NULL;
}

/*
 * 将变量加入当前作用域。
 * by_ref=1 仅用于数组/结构体形参，表示变量名自身已经是一个地址。
 */
static VarSymbol *add_var(const char *name, Type *type, int by_ref) {
  if (!current_scope) {
    push_scope();
  }
  VarSymbol *var = (VarSymbol *)xmalloc(sizeof(VarSymbol));
  var->name = xstrdup(name);
  var->type = type;
  var->by_ref = by_ref;
  var->next = current_scope->vars;
  current_scope->vars = var;
  return var;
}

/* 保持字段和形参的源码顺序，后续 PARAM 输出和字段偏移都依赖该顺序。 */
static void append_field(Field **head, Field **tail, Field *field) {
  if (!*head) {
    *head = field;
  } else {
    (*tail)->next = field;
  }
  *tail = field;
}

static Type *specifier_type(TreeNode *node);

/*
 * VarDec 是递归文法：ID 位于最深层，数组维度在回溯过程中从左到右收集。
 * 例如 int a[2][3] 会收集 sizes={2,3}。
 */
static void collect_var_dec(TreeNode *node, char **name, int *sizes,
                            int *count) {
  TreeNode *first = child_at(node, 0);
  if (is_node(first, "ID")) {
    *name = first->text;
    return;
  }
  collect_var_dec(first, name, sizes, count);
  TreeNode *size_node = child_at(node, 2);
  sizes[(*count)++] = size_node ? size_node->int_val : 0;
}

/*
 * 根据基础类型和 VarDec 构造完整类型。
 * 构造时需要从最后一个维度向外包裹，才能得到 ARRAY(size=2, elem=ARRAY(size=3,int))。
 */
static Type *var_dec_type(TreeNode *node, Type *base, char **name) {
  int sizes[64];
  int count = 0;
  *name = NULL;
  collect_var_dec(node, name, sizes, &count);
  Type *type = base;
  for (int i = count - 1; i >= 0; --i) {
    type = new_array_type(type, sizes[i]);
  }
  return type;
}

/*
 * 结构体字段偏移
 * 将结构体内的 DefList 转为字段链表，并计算每个字段的字节偏移。
 * 这里假设语义分析已经处理了字段重名和非法初始化，翻译阶段只关注布局。
 */
static Field *def_list_as_fields(TreeNode *node) {
  Field *head = NULL;
  Field *tail = NULL;
  int offset = 0;
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    TreeNode *def = child_at(cur, 0);
    Type *base = specifier_type(child_at(def, 0));
    for (TreeNode *dec_list = child_at(def, 1); dec_list;) {
      TreeNode *dec = child_at(dec_list, 0);
      char *name = NULL;
      Type *type = var_dec_type(child_at(dec, 0), base, &name);
      Field *field = new_field(name, type);
      field->offset = offset;
      offset += type_size(type);
      append_field(&head, &tail, field);
      if (child_count(dec_list) == 3) {
        dec_list = child_at(dec_list, 2);
      } else {
        break;
      }
    }
  }
  return head;
}

/*
 * 解析 StructSpecifier：
 * - STRUCT Tag 复用已有结构体布局；
 * - STRUCT OptTag { ... } 生成新布局并按需加入结构体名表。
 */
static Type *struct_specifier_type(TreeNode *node) {
  TreeNode *second = child_at(node, 1);
  if (is_node(second, "Tag")) {
    TreeNode *id = child_at(second, 0);
    StructSymbol *sym = id ? find_struct(id->text) : NULL;
    return sym ? sym->type : int_type;
  }

  const char *name = NULL;
  if (is_node(second, "OptTag")) {
    TreeNode *id = child_at(second, 0);
    name = id ? id->text : NULL;
  }
  StructSymbol *old = name ? find_struct(name) : NULL;
  if (old) {
    return old->type;
  }
  Type *type =
      new_struct_type(name, def_list_as_fields(find_child(node, "DefList")));
  add_struct(name, type);
  return type;
}

/* Specifier 是类型系统入口，统一处理基本类型和结构体类型。 */
static Type *specifier_type(TreeNode *node) {
  TreeNode *first = child_at(node, 0);
  if (is_node(first, "TYPE")) {
    return strcmp(first->text, "int") == 0 ? int_type : float_type;
  }
  if (is_node(first, "StructSpecifier")) {
    return struct_specifier_type(first);
  }
  return int_type;
}

/* 函数形参需要保留名字和完整类型，用于 PARAM 输出和函数调用传引用判断。 */
static Field *param_dec_field(TreeNode *node) {
  Type *base = specifier_type(child_at(node, 0));
  char *name = NULL;
  Type *type = var_dec_type(child_at(node, 1), base, &name);
  return new_field(name, type);
}

/* VarList 是右递归链表，这里展开成从左到右的 Field 链表。 */
static Field *var_list_fields(TreeNode *node) {
  Field *head = NULL;
  Field *tail = NULL;
  for (TreeNode *cur = node; cur;) {
    append_field(&head, &tail, param_dec_field(child_at(cur, 0)));
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
  return head;
}

/* FunDec 有无参和有参两种形式，只有四个孩子时才包含 VarList。 */
static Field *function_params(TreeNode *fun_dec) {
  if (child_count(fun_dec) == 4) {
    return var_list_fields(child_at(fun_dec, 2));
  }
  return NULL;
}

/* 函数名固定是 FunDec 的第一个孩子 ID。 */
static const char *function_name(TreeNode *fun_dec) {
  TreeNode *id = child_at(fun_dec, 0);
  return id ? id->text : "";
}

/* 第一遍只收集函数签名，不进入函数体，保证递归和后定义函数调用可翻译。 */
static void collect_ext_def(TreeNode *node) {
  if (!node) {
    return;
  }
  Type *base = specifier_type(child_at(node, 0));
  TreeNode *second = child_at(node, 1);
  TreeNode *third = child_at(node, 2);
  if (is_node(second, "FunDec") && is_node(third, "CompSt")) {
    add_function(function_name(second), base, function_params(second));
  }
}

/* 收集所有用户函数后，再加入实验三要求的 read/write 预定义函数。 */
static void collect_symbols(TreeNode *root) {
  for (TreeNode *cur = child_at(root, 0); cur; cur = child_at(cur, 1)) {
    collect_ext_def(child_at(cur, 0));
  }
  add_function("read", int_type, NULL);
  Field *write_param = new_field("x", int_type);
  add_function("write", int_type, write_param);
}

static Type *expr_type(TreeNode *node);
static void translate_exp(TreeNode *node, char *place, size_t size);
static void translate_cond(TreeNode *node, const char *label_true,
                           const char *label_false);

/* 函数调用表达式的结果类型由函数表给出；语义正确时一定能查到。 */
static Type *call_type(TreeNode *node) {
  TreeNode *id = child_at(node, 0);
  Function *func = id ? find_function(id->text) : NULL;
  return func ? func->ret : int_type;
}

/*
 * 轻量表达式类型推导。
 * 这里不再报错，只服务于代码生成决策：
 * - 判断数组/结构体表达式是否要返回地址；
 * - 计算 a[i] 的元素宽度；
 * - 计算 s.f 的字段类型。
 */
static Type *expr_type(TreeNode *node) {
  if (!node) {
    return int_type;
  }
  int n = child_count(node);
  TreeNode *c0 = child_at(node, 0);
  TreeNode *c1 = child_at(node, 1);
  TreeNode *c2 = child_at(node, 2);
  if (n == 1) {
    if (is_node(c0, "ID")) {
      VarSymbol *var = find_var(c0->text);
      return var ? var->type : int_type;
    }
    if (is_node(c0, "INT")) {
      return int_type;
    }
    return float_type;
  }
  if (n == 2) {
    return is_node(c0, "NOT") ? int_type : expr_type(c1);
  }
  if (n == 3 && is_node(c0, "LP")) {
    return expr_type(c1);
  }
  if ((n == 3 || n == 4) && is_node(c0, "ID") && is_node(c1, "LP")) {
    return call_type(node);
  }
  if (n == 3 && is_node(c1, "ASSIGNOP")) {
    return expr_type(c0);
  }
  if (n == 3 && is_node(c1, "RELOP")) {
    return int_type;
  }
  if (n == 3 && (is_node(c1, "AND") || is_node(c1, "OR"))) {
    return int_type;
  }
  if (n == 3 && (is_node(c1, "PLUS") || is_node(c1, "MINUS") ||
                 is_node(c1, "STAR") || is_node(c1, "DIV"))) {
    return expr_type(c0);
  }
  if (n == 4 && is_node(c1, "LB")) {
    Type *base = expr_type(c0);
    return base && base->kind == TYPE_ARRAY ? base->u.array.elem : int_type;
  }
  if (n == 3 && is_node(c1, "DOT")) {
    Type *base = expr_type(c0);
    Field *field = base && base->kind == TYPE_STRUCT
                       ? find_field(base->u.structure.fields, c2->text)
                       : NULL;
    return field ? field->type : int_type;
  }
  return int_type;
}

/*
 * 生成左值地址。
 *
 * 关键规则：
 * - 普通局部变量 ID 的地址为 &ID；
 * - 数组/结构体形参是引用传递，ID 本身就是地址；
 * - a[i] 的地址 = base_addr + i * elem_width；
 * - s.f 的地址 = base_addr + field_offset。
 */
static void translate_addr(TreeNode *node, char *place, size_t size) {
  int n = child_count(node);
  TreeNode *c0 = child_at(node, 0);
  TreeNode *c1 = child_at(node, 1);
  TreeNode *c2 = child_at(node, 2);

  if (n == 1 && is_node(c0, "ID")) {
    VarSymbol *var = find_var(c0->text);
    /* by_ref 形参已经保存首地址，不能再次取 &。 */
    if (var && var->by_ref) {
      snprintf(place, size, "%s", c0->text);
    } else {
      new_temp(place, size);
      ir_emit("%s := &%s", place, c0->text);
    }
    return;
  }
  if (n == 3 && is_node(c0, "LP")) {
    translate_addr(c1, place, size);
    return;
  }
  if (n == 4 && is_node(c1, "LB")) {
    char base_addr[64];
    char index[64];
    char offset[64];
    translate_addr(c0, base_addr, sizeof(base_addr));
    translate_exp(c2, index, sizeof(index));
    Type *base_type = expr_type(c0);
    /* 高维数组访问时，当前维度的元素宽度可能仍是一个数组大小。 */
    int width = base_type && base_type->kind == TYPE_ARRAY
                    ? type_size(base_type->u.array.elem)
                    : 4;
    new_temp(offset, sizeof(offset));
    ir_emit("%s := %s * #%d", offset, index, width);
    new_temp(place, size);
    ir_emit("%s := %s + %s", place, base_addr, offset);
    return;
  }
  if (n == 3 && is_node(c1, "DOT")) {
    char base_addr[64];
    translate_addr(c0, base_addr, sizeof(base_addr));
    Type *base_type = expr_type(c0);
    Field *field = base_type && base_type->kind == TYPE_STRUCT
                       ? find_field(base_type->u.structure.fields, c2->text)
                       : NULL;
    int offset = field ? field->offset : 0;
    /* 偏移为 0 时直接复用基地址，减少一条冗余加法 IR。 */
    if (offset == 0) {
      snprintf(place, size, "%s", base_addr);
    } else {
      new_temp(place, size);
      ir_emit("%s := %s + #%d", place, base_addr, offset);
    }
    return;
  }
  translate_exp(node, place, size);
}

/* ARG 要按反序输出，所以实参用头插法加入链表。 */
static void append_arg(ArgItem **head, const char *place) {
  ArgItem *item = (ArgItem *)xmalloc(sizeof(ArgItem));
  snprintf(item->place, sizeof(item->place), "%s", place);
  item->next = *head;
  *head = item;
}

/*
 * 翻译实参列表。
 * 基本类型按值传递；数组和结构体按引用传递，因此传入首地址。
 */
static void translate_args(TreeNode *node, ArgItem **head) {
  for (TreeNode *cur = node; cur;) {
    TreeNode *exp = child_at(cur, 0);
    char arg[64];
    if (is_ref_type(expr_type(exp))) {
      translate_addr(exp, arg, sizeof(arg));
    } else {
      translate_exp(exp, arg, sizeof(arg));
    }
    append_arg(head, arg);
    if (child_count(cur) == 3) {
      cur = child_at(cur, 2);
    } else {
      break;
    }
  }
}

/*
 * 函数调用翻译。
 * read/write 是实验三内建 I/O，直接生成 READ/WRITE；
 * 普通函数先输出所有 ARG，再用 CALL 取得返回值。
 */
static void translate_call(TreeNode *node, char *place, size_t size) {
  TreeNode *id = child_at(node, 0);
  const char *name = id ? id->text : "";
  if (strcmp(name, "read") == 0) {
    new_temp(place, size);
    ir_emit("READ %s", place);
    return;
  }
  if (strcmp(name, "write") == 0) {
    char arg[64];
    translate_exp(child_at(child_at(node, 2), 0), arg, sizeof(arg));
    ir_emit("WRITE %s", arg);
    /* write 在语义上返回 int 0，若出现在表达式中仍需给调用结果一个值。 */
    new_temp(place, size);
    ir_emit("%s := #0", place);
    return;
  }

  ArgItem *args = NULL;
  if (child_count(node) == 4) {
    translate_args(child_at(node, 2), &args);
  }
  for (ArgItem *arg = args; arg; arg = arg->next) {
    ir_emit("ARG %s", arg->place);
  }
  new_temp(place, size);
  ir_emit("%s := CALL %s", place, name);
}

/*
 * 赋值翻译。
 * 基本变量可直接生成 x := value；数组元素、结构体字段等复合左值
 * 需要先计算左值地址，再生成 *addr := value。
 */
static void translate_assign(TreeNode *node, char *place, size_t size) {
  TreeNode *lhs = child_at(node, 0);
  TreeNode *rhs = child_at(node, 2);
  char value[64];
  translate_exp(rhs, value, sizeof(value));

  Type *lhs_type = expr_type(lhs);
  int lhs_is_id = child_count(lhs) == 1 && is_node(child_at(lhs, 0), "ID");
  if (lhs_is_id && lhs_type && lhs_type->kind == TYPE_BASIC) {
    const char *name = child_at(lhs, 0)->text;
    ir_emit("%s := %s", name, value);
    snprintf(place, size, "%s", name);
  } else {
    char addr[64];
    translate_addr(lhs, addr, sizeof(addr));
    ir_emit("*%s := %s", addr, value);
    snprintf(place, size, "%s", value);
  }
}

/*
 * 表达式翻译入口，place 返回该表达式结果所在的操作数名。
 * 对立即数和变量，place 可以直接是 "#1" 或 "x"；
 * 对复杂表达式，通常新建临时变量并追加相应 IR。
 */
static void translate_exp(TreeNode *node, char *place, size_t size) {
  int n = child_count(node);
  TreeNode *c0 = child_at(node, 0);
  TreeNode *c1 = child_at(node, 1);
  TreeNode *c2 = child_at(node, 2);

  if (n == 1) {
    if (is_node(c0, "ID")) {
      snprintf(place, size, "%s", c0->text);
      return;
    }
    if (is_node(c0, "INT")) {
      snprintf(place, size, "#%d", c0->int_val);
      return;
    }
  }
  if (n == 2 && is_node(c0, "MINUS")) {
    char value[64];
    translate_exp(c1, value, sizeof(value));
    new_temp(place, size);
    ir_emit("%s := #0 - %s", place, value);
    return;
  }
  if (n == 2 && is_node(c0, "NOT")) {
    char true_label[64];
    char false_label[64];
    new_label(true_label, sizeof(true_label));
    new_label(false_label, sizeof(false_label));
    new_temp(place, size);
    ir_emit("%s := #0", place);
    /* 逻辑表达式先按控制流翻译，再在真分支把结果改为 1。 */
    translate_cond(node, true_label, false_label);
    ir_emit("LABEL %s :", true_label);
    ir_emit("%s := #1", place);
    ir_emit("LABEL %s :", false_label);
    return;
  }
  if (n == 3 && is_node(c0, "LP")) {
    translate_exp(c1, place, size);
    return;
  }
  if ((n == 3 || n == 4) && is_node(c0, "ID") && is_node(c1, "LP")) {
    translate_call(node, place, size);
    return;
  }
  if (n == 3 && is_node(c1, "ASSIGNOP")) {
    translate_assign(node, place, size);
    return;
  }
  if (n == 3 && (is_node(c1, "RELOP") || is_node(c1, "AND") ||
                 is_node(c1, "OR"))) {
    char true_label[64];
    char false_label[64];
    new_label(true_label, sizeof(true_label));
    new_label(false_label, sizeof(false_label));
    new_temp(place, size);
    ir_emit("%s := #0", place);
    /* 当条件表达式作为普通值使用时，将控制流结果物化为 0/1。 */
    translate_cond(node, true_label, false_label);
    ir_emit("LABEL %s :", true_label);
    ir_emit("%s := #1", place);
    ir_emit("LABEL %s :", false_label);
    return;
  }
  if (n == 3 && (is_node(c1, "PLUS") || is_node(c1, "MINUS") ||
                 is_node(c1, "STAR") || is_node(c1, "DIV"))) {
    char left[64];
    char right[64];
    const char *op = is_node(c1, "PLUS")    ? "+"
                     : is_node(c1, "MINUS") ? "-"
                     : is_node(c1, "STAR")  ? "*"
                                             : "/";
    translate_exp(c0, left, sizeof(left));
    translate_exp(c2, right, sizeof(right));
    new_temp(place, size);
    ir_emit("%s := %s %s %s", place, left, op, right);
    return;
  }
  if ((n == 4 && is_node(c1, "LB")) || (n == 3 && is_node(c1, "DOT"))) {
    /*
     * 如果访问结果仍是数组/结构体，表达式值应为地址；
     * 如果结果是基本类型，则需要从计算出的地址取值。
     */
    if (is_ref_type(expr_type(node))) {
      translate_addr(node, place, size);
    } else {
      char addr[64];
      translate_addr(node, addr, sizeof(addr));
      new_temp(place, size);
      ir_emit("%s := *%s", place, addr);
    }
    return;
  }
  snprintf(place, size, "#0");
}

/*
 * 条件表达式翻译，直接生成跳转到 label_true / label_false 的代码。
 * 这样 if/while 可以自然复用，并且 AND/OR 能按短路语义生成控制流。
 */
static void translate_cond(TreeNode *node, const char *label_true,
                           const char *label_false) {
  int n = child_count(node);
  TreeNode *c0 = child_at(node, 0);
  TreeNode *c1 = child_at(node, 1);
  TreeNode *c2 = child_at(node, 2);
  if (n == 3 && is_node(c1, "RELOP")) {
    char left[64];
    char right[64];
    translate_exp(c0, left, sizeof(left));
    translate_exp(c2, right, sizeof(right));
    ir_emit("IF %s %s %s GOTO %s", left, c1->text, right, label_true);
    ir_emit("GOTO %s", label_false);
    return;
  }
  if (n == 3 && is_node(c1, "AND")) {
    char label_mid[64];
    new_label(label_mid, sizeof(label_mid));
    /* AND：左边为真才继续判断右边，左边为假直接跳 false。 */
    translate_cond(c0, label_mid, label_false);
    ir_emit("LABEL %s :", label_mid);
    translate_cond(c2, label_true, label_false);
    return;
  }
  if (n == 3 && is_node(c1, "OR")) {
    char label_mid[64];
    new_label(label_mid, sizeof(label_mid));
    /* OR：左边为真直接跳 true，左边为假才继续判断右边。 */
    translate_cond(c0, label_true, label_mid);
    ir_emit("LABEL %s :", label_mid);
    translate_cond(c2, label_true, label_false);
    return;
  }
  if (n == 2 && is_node(c0, "NOT")) {
    translate_cond(c1, label_false, label_true);
    return;
  }
  char value[64];
  translate_exp(node, value, sizeof(value));
  /* 非关系表达式作为条件时，按 value != 0 判断真假。 */
  ir_emit("IF %s != #0 GOTO %s", value, label_true);
  ir_emit("GOTO %s", label_false);
}

static void translate_stmt(TreeNode *node);

/*
 * 局部定义翻译。
 * 基本变量无需 DEC；数组和结构体需要申请连续空间。
 * 初始化只对语义允许的普通变量生效。
 */
static void translate_def(TreeNode *node) {
  Type *base = specifier_type(child_at(node, 0));
  for (TreeNode *dec_list = child_at(node, 1); dec_list;) {
    TreeNode *dec = child_at(dec_list, 0);
    char *name = NULL;
    Type *type = var_dec_type(child_at(dec, 0), base, &name);
    add_var(name, type, 0);
    if (is_ref_type(type)) {
      ir_emit("DEC %s %d", name, type_size(type));
    }
    if (child_count(dec) == 3) {
      char value[64];
      translate_exp(child_at(dec, 2), value, sizeof(value));
      ir_emit("%s := %s", name, value);
    }
    if (child_count(dec_list) == 3) {
      dec_list = child_at(dec_list, 2);
    } else {
      break;
    }
  }
}

/* DefList 是右递归链表，按源码顺序逐个翻译局部定义。 */
static void translate_def_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    translate_def(child_at(cur, 0));
  }
}

/* StmtList 同样是右递归链表，保持语句顺序生成 IR。 */
static void translate_stmt_list(TreeNode *node) {
  for (TreeNode *cur = node; cur; cur = child_at(cur, 1)) {
    translate_stmt(child_at(cur, 0));
  }
}

/*
 * 复合语句翻译。
 * 函数体作用域由 translate_function 创建；嵌套语句块需要在这里单独建作用域。
 */
static void translate_comp_st(TreeNode *node, int create_scope) {
  if (create_scope) {
    push_scope();
  }
  translate_def_list(find_child(node, "DefList"));
  translate_stmt_list(find_child(node, "StmtList"));
  if (create_scope) {
    pop_scope();
  }
}

/*
 * 语句翻译。
 * 控制流语句统一用 label_true / label_false / label_end 组合出三地址码。
 */
static void translate_stmt(TreeNode *node) {
  if (!node) {
    return;
  }
  int n = child_count(node);
  TreeNode *first = child_at(node, 0);
  if (is_node(first, "Exp")) {
    char place[64];
    translate_exp(first, place, sizeof(place));
    return;
  }
  if (is_node(first, "RETURN")) {
    char value[64];
    translate_exp(child_at(node, 1), value, sizeof(value));
    ir_emit("RETURN %s", value);
    return;
  }
  if (is_node(first, "CompSt")) {
    translate_comp_st(first, 1);
    return;
  }
  if (is_node(first, "IF")) {
    char label_true[64];
    char label_false[64];
    new_label(label_true, sizeof(label_true));
    new_label(label_false, sizeof(label_false));
    if (n == 5) {
      /* if without else：条件假时直接落到 false 标签之后。 */
      translate_cond(child_at(node, 2), label_true, label_false);
      ir_emit("LABEL %s :", label_true);
      translate_stmt(child_at(node, 4));
      ir_emit("LABEL %s :", label_false);
    } else {
      char label_end[64];
      new_label(label_end, sizeof(label_end));
      /* if-else：then 分支结束后跳过 else 分支。 */
      translate_cond(child_at(node, 2), label_true, label_false);
      ir_emit("LABEL %s :", label_true);
      translate_stmt(child_at(node, 4));
      ir_emit("GOTO %s", label_end);
      ir_emit("LABEL %s :", label_false);
      translate_stmt(child_at(node, 6));
      ir_emit("LABEL %s :", label_end);
    }
    return;
  }
  if (is_node(first, "WHILE")) {
    char label_begin[64];
    char label_true[64];
    char label_false[64];
    new_label(label_begin, sizeof(label_begin));
    new_label(label_true, sizeof(label_true));
    new_label(label_false, sizeof(label_false));
    /* while：先标记循环头，条件真进入循环体，循环体结束跳回循环头。 */
    ir_emit("LABEL %s :", label_begin);
    translate_cond(child_at(node, 2), label_true, label_false);
    ir_emit("LABEL %s :", label_true);
    translate_stmt(child_at(node, 4));
    ir_emit("GOTO %s", label_begin);
    ir_emit("LABEL %s :", label_false);
  }
}

/*
 * 函数定义翻译。
 * FUNCTION 后立即输出 PARAM；数组/结构体形参标记为 by_ref，
 * 后续访问这些形参时会把形参变量名当作地址使用。
 */
static void translate_function(TreeNode *ext_def) {
  Type *ret = specifier_type(child_at(ext_def, 0));
  (void)ret;
  TreeNode *fun_dec = child_at(ext_def, 1);
  TreeNode *comp_st = child_at(ext_def, 2);
  ir_emit("FUNCTION %s :", function_name(fun_dec));
  push_scope();
  Field *params = function_params(fun_dec);
  for (Field *param = params; param; param = param->next) {
    add_var(param->name, param->type, is_ref_type(param->type));
    ir_emit("PARAM %s", param->name);
  }
  translate_comp_st(comp_st, 0);
  pop_scope();
}

/* 顶层只翻译函数定义；全局变量在实验三假设中不会使用。 */
static void translate_ext_def(TreeNode *node) {
  TreeNode *second = child_at(node, 1);
  TreeNode *third = child_at(node, 2);
  if (is_node(second, "FunDec") && is_node(third, "CompSt")) {
    translate_function(node);
  }
}

/*
 * 翻译入口。
 * 步骤：
 * 1. 重置 IR 和所有翻译状态；
 * 2. 第一遍收集结构体布局和函数签名；
 * 3. 第二遍按源码顺序翻译函数体；
 * 4. 将 IR 链表写入输出文件。
 */
void translate_program(TreeNode *root, FILE *out) {
  ir_reset();
  structs = NULL;
  functions = NULL;
  current_scope = NULL;
  temp_no = 1;
  label_no = 1;
  int_type = new_basic_type(BASIC_INT);
  float_type = new_basic_type(BASIC_FLOAT);

  collect_symbols(root);
  for (TreeNode *cur = child_at(root, 0); cur; cur = child_at(cur, 1)) {
    translate_ext_def(child_at(cur, 0));
  }
  ir_print(out);
}

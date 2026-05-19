#include "ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 实验三要求输出线性三地址码。这里使用单链表保存每一行 IR：
 * - 生成阶段只做尾插，保持翻译顺序；
 * - 输出阶段顺序遍历即可；
 * - 相比边翻译边 fprintf，后续若需要做简单优化或删除冗余代码，也有调整空间。
 */
// 每一个 IrLine 节点代表一行 IR
typedef struct IrLine {
  char *text; // text 保存这一行到字符串内容
  struct IrLine *next; // next 指向下一行 IR
} IrLine;

static IrLine *head = NULL;
static IrLine *tail = NULL;

/* 统一内存分配入口，失败时立即终止，避免后续空指针传播。 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  memset(p, 0, size);
  return p;
}

/* strdup 不是 ISO C 标准的一部分，这里保留一个本地实现。 */
static char *xstrdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *p = (char *)xmalloc(len);
  memcpy(p, s, len);
  return p;
}

/* 每次翻译一个新 Program 前清空旧链表，便于同一进程内重复调用。 */
void ir_reset(void) {
  IrLine *cur = head;
  while (cur) {
    IrLine *next = cur->next;
    free(cur->text);
    free(cur);
    cur = next;
  }
  head = NULL;
  tail = NULL;
}

/*
 * 追加一行 IR。调用者传入的格式串应直接对应实验教程要求的输出形式，
 * 例如 "x := y + z"、"IF x < y GOTO label1"。
 */
void ir_emit(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  IrLine *line = (IrLine *)xmalloc(sizeof(IrLine));
  line->text = xstrdup(buf);
  if (!head) {
    head = line;
  } else {
    tail->next = line;
  }
  tail = line;
}

/* 将内存中的线性 IR 按顺序写入文件，每行一条中间代码。 */
void ir_print(FILE *out) {
  for (IrLine *cur = head; cur; cur = cur->next) {
    fprintf(out, "%s\n", cur->text);
  }
}

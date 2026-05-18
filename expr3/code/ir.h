#ifndef IR_H
#define IR_H

#include <stdio.h>

/*
 * IR 模块只负责保存和打印“已经格式化好的三地址码文本”。
 * 翻译阶段通过 ir_emit 追加一行，最后由 ir_print 按插入顺序写入输出文件。
 */
void ir_reset(void);
void ir_emit(const char *fmt, ...);
void ir_print(FILE *out);

#endif

#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "tree.h"
#include <stdio.h>

/*
 * 从实验一/二构造出的语法树生成实验三三地址码。
 * 调用前应已完成词法、语法和语义检查；本模块默认输入程序语义正确。
 */
void translate_program(TreeNode *root, FILE *out);

#endif

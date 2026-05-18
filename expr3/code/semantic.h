#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "tree.h"

/*
 * 对语法分析阶段构造出的 Program 根节点执行语义分析。
 * 该函数只负责输出语义错误；若程序语义正确，则不输出任何内容。
 */
void semantic_analyze(TreeNode *root);

#endif

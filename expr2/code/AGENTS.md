# Repository Guidelines

## Project Structure & Module Organization

This repository contains the code for Compiler Principles Project 2, a C-- semantic analyzer built on Flex and Bison.

- `lexical.l`: Flex lexer. Produces tokens and lexical errors.
- `syntax.y`: Bison parser. Builds the syntax tree and includes grammar recovery rules.
- `tree.h`, `tree.c`: Syntax tree node definitions and tree construction helpers.
- `semantic.h`, `semantic.c`: Semantic analysis, type checking, symbol tables, scope handling, and error reporting.
- `main.c`: Entry point. Opens the input file, runs parsing, then semantic analysis.
- `Makefile`: Local build and cleanup commands.
- `../实验教程_Project_2.pdf`: Project requirements.
- `../report/`: Report materials.

Generated files such as `cc`, `lex.yy.c`, `syntax.tab.c`, `syntax.tab.h`, and `syntax.output` are build artifacts and should not be edited by hand.

## Build, Test, and Development Commands

- `make`: Generates lexer/parser sources and builds the executable `./cc`.
- `./cc path/to/input.cmm`: Runs semantic analysis on one C-- source file.
- `make clean`: Removes generated build artifacts.

Example workflow:

```sh
make
./cc test/example.cmm
make clean
```

## Coding Style & Naming Conventions

Use C with `-Wall -Wextra -std=gnu11`, as configured in `Makefile`. Prefer small helper functions for tree traversal, type construction, and symbol lookup. Keep public declarations in headers and implementation details in `.c` files.

Follow the existing naming style:

- Types and structs: `TreeNode`, `Type`, `Field`.
- Functions and variables: `snake_case`, for example `semantic_analyze`, `child_at`.
- Constants/macros: uppercase, for example `HASH_SIZE`.

Use clear, short comments for non-obvious parser recovery or semantic rules. Keep generated Flex/Bison files out of manual edits.

## Testing Guidelines

There is no formal test framework. Test with `.cmm` input files that exercise each required semantic error and valid program cases. The expected behavior is:

- Valid input: no stdout output.
- Invalid input: `Error type [N] at Line [line]: [message].`

Before submitting, run representative cases for error types `1-19`, including function declarations, nested scopes, arrays, and structures.

## Commit & Pull Request Guidelines

Recent commits use concise conventional-style prefixes, often in Chinese, such as `feat: 实现实验二：语义分析` and `chore: 调整gitignore`. Keep commit messages short and action-oriented:

- `feat: add semantic type checking`
- `fix: correct array assignment comparison`
- `chore: clean generated artifacts`

For pull requests, include a brief summary, the tested commands or input cases, and any known limitations. Do not include generated parser or lexer artifacts unless the submission process explicitly requires them.

# Repository Guidelines

## Project Structure & Module Organization
This repository contains a small C-based compiler front end built with Flex and Bison.

- `lexical.l`: lexer rules, token creation, and lexical error handling.
- `syntax.y`: grammar, precedence rules, parse tree construction, and syntax errors.
- `tree.h` and `tree.c`: parse tree node definitions and printing helpers.
- `main.c`: CLI entry point; reads one `.cmm` file and prints the tree when parsing succeeds.
- `test/`: manual test corpus. Files use numbered names such as `03_correct_inc.cmm` and cover both valid and invalid inputs.

Generated files such as `lex.yy.c`, `syntax.tab.c`, `syntax.tab.h`, `syntax.output`, and the `cc` binary should not be edited by hand.

## Build, Test, and Development Commands
- `make`: run Bison and Flex, then build the `cc` executable with `gcc`.
- `make clean`: remove generated parser files and the compiled binary.
- `./cc test/03_correct_inc.cmm`: parse a sample input and print the syntax tree.

When changing `lexical.l` or `syntax.y`, rebuild with `make` so generated sources stay in sync. Keep grammar files ASCII-only; a full-width `｜` in `syntax.y` currently breaks `bison`.

## Coding Style & Naming Conventions
Follow the existing C style:

- Use 2-space indentation in `.c` and `.h` files.
- Keep function and variable names in `snake_case`, for example `new_float_node` and `last_error_line`.
- Use uppercase token and grammar symbol names where the parser already does so, such as `INT`, `FLOAT`, and `Exp`.
- Prefer small, single-purpose helper functions over inlined repeated logic.

There is no formatter or linter configured here, so match surrounding code closely.

## Testing Guidelines
There is no automated test framework yet. Validate parser changes by running `./cc` against representative files in `test/`, including both success and error cases. Add new `.cmm` fixtures to `test/` when fixing bugs or extending grammar support, and keep the numbered filename pattern consistent.

## Commit & Pull Request Guidelines
Recent history uses short prefixes such as `chore:`, `finish(4):`, and `not finish`. Prefer concise, imperative commits like `fix: reject invalid hex literals` or `parser: recover after missing ]`.

Pull requests should include:

- a brief summary of the grammar or lexer change,
- the test files added or exercised,
- sample output when behavior changes,
- and any known parser limitations or recovery gaps.

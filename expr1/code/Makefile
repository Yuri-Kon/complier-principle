CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11

all: cc

cc: lex.yy.c syntax.tab.c tree.c main.c
	$(CC) $(CFLAGS) -o $@ $^ -lfl

syntax.tab.c syntax.tab.h: syntax.y
	bison -d -v syntax.y

lex.yy.c: lexical.l syntax.tab.h
	flex lexical.l

clean:
	rm -f cc lex.yy.c syntax.tab.c syntax.tab.h syntax.output

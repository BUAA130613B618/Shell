user-sh : bison.tab.o execute.o lex.yy.o
	cc -o user-sh bison.tab.o execute.o lex.yy.o
bison.tab.o : bison.tab.c global.h
	cc -c bison.tab.c
lex.yy.o : lex.yy.c global.h
	cc -c lex.yy.c -lfl
execute.o : execute.c global.h
	cc -c execute.c
bison.tab.h : bison.y
	bison -d bison.y
bison.tab.c : bison.y
	bison -d bison.y
lex.yy.c : flex.l bison.tab.h
	flex flex.l
clean :
	rm user-sh bison.tab.o execute.o bison.tab.c bison.tab.h lex.yy.c lex.yy.o

# lex.yy.c
# y.tab.c
# par.l
# par.y
CC=clang
CFLAGS=-O0 -g -Wall -Wextra
OBJS=state.o value.o memory.o hash_map.o skip_list.o closure.o
OBJT=$(OBJS) yut_rand.o yut.o main_test.o

test: yut_test memory_test value_test skip_list_test hash_map_test
	./memory_test && ./value_test && ./skip_list_test && ./hash_map_test

memory_test: $(OBJT) memory_test.o
	$(CC) $(OBJT) memory_test.o -o memory_test

memory_test.o: memory_test.c memory.h state.h
	$(CC) $(CFLAGS) memory_test.c -c -o memory_test.o

value_test: $(OBJT) value_test.o
	$(CC) $(OBJT) value_test.o -o value_test

value_test.o: value_test.c value.h memory.h state.h
	$(CC) $(CFLAGS) value_test.c -c -o value_test.o

skip_list_test: $(OBJT) skip_list_test.o
	$(CC) $(OBJT) skip_list_test.o -o skip_list_test

skip_list_test.o: skip_list_test.c value.h memory.h state.h
	$(CC) $(CFLAGS) skip_list_test.c -c -o skip_list_test.o

hash_map_test: $(OBJT) hash_map_test.o
	$(CC) $(OBJT) hash_map_test.o -o hash_map_test

hash_map_test.o: hash_map_test.c value.h memory.h state.h
	$(CC) $(CFLAGS) hash_map_test.c -c -o hash_map_test.o

main_test.o: main_test.c state.h yut.h
	$(CC) $(CFLAGS) main_test.c -c -o main_test.o

yut_test: yut_test.o yut_rand.o yut.o
	$(CC) yut_test.o yut_rand.o yut.o -o yut_test

yut_test.o: yut_test.c yut_rand.h yut.h
	$(CC) $(CFLAGS) yut_test.c -c -o yut_test.o

yut_rand.o: yut_rand.c yut_rand.h yut.h
	$(CC) $(CFLAGS) yut_rand.c -c -o yut_rand.o

yut.o: yut.c yut.h
	$(CC) $(CFLAGS) yut.c -c -o yut.o

state.o: state.c state.h value.h memory.h
	$(CC) $(CFLAGS) state.c -c -o state.o

closure.o: closure.c state.h value.h memory.h
	$(CC) $(CFLAGS) closure.c -c -o closure.o

skip_list.o: skip_list.c state.h value.h memory.h
	$(CC) $(CFLAGS) skip_list.c -c -o skip_list.o
	
hash_map.o: hash_map.c state.h value.h memory.h
	$(CC) $(CFLAGS) hash_map.c -c -o hash_map.o

value.o: value.c value.h state.h
	$(CC) $(CFLAGS) value.c -c -o value.o

memory.o: memory.c memory.h state.h
	$(CC) $(CFLAGS) memory.c -c -o memory.o

par: lex.yy.o y.tab.o
	$(CC) lex.yy.o y.tab.o -o par

y.tab.o: y.tab.c y.tab.h
	gcc -g -DYYERROR_VERBOSE y.tab.c -c -o y.tab.o

lex.yy.o: lex.yy.c y.tab.c
	gcc -g -DYYERROR_VERBOSE lex.yy.c -c -o lex.yy.o

y.tab.c: par.y
	yacc par.y -d

lex.yy.c: par.l
	lex par.l

clean:
	rm *.o *_test y.tab.c y.tab.h lex.yy.c par 2&> /dev/null

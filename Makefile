CC=gcc
CFLAGS=-O0 -g -Wall -Wextra
OBJS=state.o value.o memory.o hash_map.o skip_list.o closure.o call.o
OBJT=$(OBJS) yut_rand.o yut.o main_test.o disassembly.o parser.o lexer.o symbol.o

test: yut_test memory_test value_test skip_list_test hash_map_test \
      closure_test
	./memory_test && ./value_test && ./skip_list_test && ./hash_map_test && \
	./closure_test

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

closure_test: $(OBJT) closure_test.o
	$(CC) $(OBJT) closure_test.o -o closure_test

closure_test.o: closure_test.c value.h memory.h state.h
	$(CC) $(CFLAGS) closure_test.c -c -o closure_test.o

call_test: $(OBJT) call_test.o 
	$(CC) $(OBJT) call_test.o -o call_test

call_test.o: call_test.c value.h memory.h state.h
	$(CC) $(CFLAGS) call_test.c -c -o call_test.o

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

disassembly.o: disassembly.c disassembly.h assembly.h value.h
	$(CC) $(CFLAGS) disassembly.c -c -o disassembly.o

symbol.o: symbol.c symbol.h state.h value.h
	$(CC) $(CFLAGS) symbol.c -c -o symbol.o

state.o: state.c state.h value.h memory.h
	$(CC) $(CFLAGS) state.c -c -o state.o

closure.o: closure.c state.h value.h memory.h
	$(CC) $(CFLAGS) closure.c -c -o closure.o

skip_list.o: skip_list.c state.h value.h memory.h
	$(CC) $(CFLAGS) skip_list.c -c -o skip_list.o
	
hash_map.o: hash_map.c state.h value.h memory.h
	$(CC) $(CFLAGS) hash_map.c -c -o hash_map.o

call.o: call.c state.h value.h memory.h
	$(CC) $(CFLAGS) call.c -c -o call.o

value.o: value.c value.h state.h
	$(CC) $(CFLAGS) value.c -c -o value.o

memory.o: memory.c memory.h state.h
	$(CC) $(CFLAGS) memory.c -c -o memory.o

parser.o: y.tab.c y.tab.h parser.y
	$(CC) $(CFLAGS) -DYYERROR_VERBOSE y.tab.c -c -o parser.o

lexer.o: lex.yy.c y.tab.c parser.l
	$(CC) $(CFLAGS) -DYYERROR_VERBOSE lex.yy.c -c -o lexer.o

y.tab.c: parser.y
	yacc parser.y -d

lex.yy.c: parser.l
	lex parser.l

clean:
	rm *.o *_test y.tab.c y.tab.h lex.yy.c par 2&> /dev/null

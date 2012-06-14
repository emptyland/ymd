CC=clang
DFLAGS=-O0 -g
CFLAGS=$(DFLAGS) -Wall -Wextra
OBJS=state.o value.o memory.o dynamic_array.o hash_map.o skip_list.o closure.o call.o libc.o
OBJT=$(OBJS) yut_rand.o yut.o main_test.o disassembly.o parser.o lexer.o symbol.o
INCS=state.h value.h memory.h
INCT=$(INCS) yut.h yut_rand.h

#-------------------------------------------------------------------------------
# Run all test
test: yut_test memory_test value_test skip_list_test hash_map_test \
      closure_test dynamic_array_test
	./memory_test && ./value_test && ./skip_list_test && ./hash_map_test && \
	./closure_test && ./dynamic_array_test

#-------------------------------------------------------------------------------
# Unit test rules:
memory_test: $(OBJT) memory_test.o
	$(CC) $(OBJT) memory_test.o -o memory_test

memory_test.o: $(INCT) memory_test.c
	$(CC) $(CFLAGS) memory_test.c -c -o memory_test.o

value_test: $(OBJT) value_test.o
	$(CC) $(OBJT) value_test.o -o value_test

value_test.o: $(INCT) value_test.c 
	$(CC) $(CFLAGS) value_test.c -c -o value_test.o

skip_list_test: $(OBJT) skip_list_test.o
	$(CC) $(OBJT) skip_list_test.o -o skip_list_test

skip_list_test.o: $(INCT) skip_list_test.c 
	$(CC) $(CFLAGS) skip_list_test.c -c -o skip_list_test.o

hash_map_test: $(OBJT) hash_map_test.o
	$(CC) $(OBJT) hash_map_test.o -o hash_map_test

hash_map_test.o: $(INCT) hash_map_test.c 
	$(CC) $(CFLAGS) hash_map_test.c -c -o hash_map_test.o

dynamic_array_test: $(OBJT) dynamic_array_test.o
	$(CC) $(OBJT) dynamic_array_test.o -o dynamic_array_test

dynamic_array_test.o: $(INCT) dynamic_array_test.c
	$(CC) $(CFLAGS) dynamic_array_test.c -c -o dynamic_array_test.o

closure_test: $(OBJT) closure_test.o
	$(CC) $(OBJT) closure_test.o -o closure_test

closure_test.o: $(INCT) closure_test.c
	$(CC) $(CFLAGS) closure_test.c -c -o closure_test.o

call_test: $(OBJT) call_test.o 
	$(CC) $(OBJT) call_test.o -o call_test

call_test.o: $(INCT) libc.h call_test.c 
	$(CC) $(CFLAGS) call_test.c -c -o call_test.o

main_test.o: main_test.c state.h yut.h
	$(CC) $(CFLAGS) main_test.c -c -o main_test.o

#-------------------------------------------------------------------------------
# Unit Test Tookit rules:
yut_test: yut_test.o yut_rand.o yut.o
	$(CC) yut_test.o yut_rand.o yut.o -o yut_test

yut_test.o: yut_test.c yut_rand.h yut.h
	$(CC) $(CFLAGS) yut_test.c -c -o yut_test.o

yut_rand.o: yut_rand.c yut_rand.h yut.h
	$(CC) $(CFLAGS) yut_rand.c -c -o yut_rand.o

yut.o: yut.c yut.h
	$(CC) $(CFLAGS) yut.c -c -o yut.o

# ymd_main
#-------------------------------------------------------------------------------
ymd_main: $(OBJS) ymd_main.o disassembly.o parser.o lexer.o symbol.o
	$(CC) $(OBJS) ymd_main.o disassembly.o parser.o lexer.o symbol.o -o ymd_main

ymd_main.o: $(INCS) disassembly.h libc.h ymd_main.c
	$(CC) $(CFLAGS) ymd_main.c -c -o ymd_main.o

# Objects rules:
#-------------------------------------------------------------------------------
# Objects rules:
disassembly.o: disassembly.c disassembly.h assembly.h value.h
	$(CC) $(CFLAGS) disassembly.c -c -o disassembly.o

symbol.o: symbol.c symbol.h state.h value.h
	$(CC) $(CFLAGS) symbol.c -c -o symbol.o

state.o: state.c state.h value.h memory.h
	$(CC) $(CFLAGS) state.c -c -o state.o

closure.o: $(INCS) closure.c
	$(CC) $(CFLAGS) closure.c -c -o closure.o

skip_list.o: $(INCS) skip_list.c
	$(CC) $(CFLAGS) skip_list.c -c -o skip_list.o

dynamic_array.o: $(INCS) dynamic_array.c 
	$(CC) $(CFLAGS) dynamic_array.c -c -o dynamic_array.o

hash_map.o: $(INCS) hash_map.c
	$(CC) $(CFLAGS) hash_map.c -c -o hash_map.o

call.o: $(INCS) call.c
	$(CC) $(CFLAGS) call.c -c -o call.o

libc.o: $(INCS) libc.c
	$(CC) $(CFLAGS) libc.c -c -o libc.o

value.o: value.c value.h state.h
	$(CC) $(CFLAGS) value.c -c -o value.o

memory.o: memory.c memory.h state.h
	$(CC) $(CFLAGS) memory.c -c -o memory.o

#-------------------------------------------------------------------------------
# Parser and Lexer rules:
parser.o: y.tab.c y.tab.h parser.y
	$(CC) $(CFLAGS) -DYYERROR_VERBOSE y.tab.c -c -o parser.o

lexer.o: lex.yy.c y.tab.c parser.l
	$(CC) $(DFLAGS) -DYYERROR_VERBOSE lex.yy.c -c -o lexer.o

y.tab.c: parser.y
	yacc parser.y -d

lex.yy.c: parser.l
	lex parser.l

clean:
	rm *.o *_test y.tab.c y.tab.h lex.yy.c ymd_main 2&> /dev/null

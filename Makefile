include config.mk
OBJS=state.o value.o memory.o dynamic_array.o hash_map.o skip_list.o closure.o \
	 call.o libc.o libtest.o encoding.o compiler.o lex.o tostring.o string.o \
	 libos.o pickle.o
OBJX=print.o disassembly.o
OBJI=yut_rand.o yut.o main_test.o
OBJT=$(OBJS) $(OBJI)
INCS=state.h value.h value_inl.h memory.h builtin.h core.h
INCT=$(INCS) yut.h yut_rand.h

LIB_REGEX=3rd/regex/libregex.a
XLIBS=$(LIB_REGEX)

#-------------------------------------------------------------------------------
# ymd_main
ymd_main: $(OBJS) $(OBJX) $(XLIBS) ymd_main.o 
	$(CC) $(OBJS) $(OBJX) $(XLIBS) ymd_main.o -o ymd_main

ymd_main.o: $(INCS) disassembly.h libc.h ymd_main.c
	$(CC) $(CFLAGS) ymd_main.c -c -o ymd_main.o

#-------------------------------------------------------------------------------
# Run all test
test: yut_test memory_test value_test skip_list_test hash_map_test \
      closure_test dynamic_array_test encoding_test lex_test zstream_test \
	  pickle_test
	./memory_test && ./value_test && ./skip_list_test && ./hash_map_test && \
	./closure_test && ./dynamic_array_test && ./encoding_test && ./lex_test && \
	./zstream_test && ./pickle_test

#-------------------------------------------------------------------------------
# Unit test rules:
pickle_test: $(OBJT) $(OBJX) pickle_test.o
	$(CC) $(OBJT) $(OBJX) pickle_test.o -o pickle_test

pickle_test.o: $(INCT) pickle_test.c pickle.h zstream.h
	$(CC) $(CFLAGS) pickle_test.c -c -o pickle_test.o

zstream_test: $(OBJT) $(OBJX) zstream_test.o
	$(CC) $(OBJT) $(OBJX) zstream_test.o -o zstream_test

zstream_test.o: $(INCT) zstream_test.c zstream.h
	$(CC) $(CFLAGS) zstream_test.c -c -o zstream_test.o

memory_test: $(OBJT) $(OBJX) memory_test.o
	$(CC) $(OBJT) $(OBJX) memory_test.o -o memory_test

memory_test.o: $(INCT) memory_test.c
	$(CC) $(CFLAGS) memory_test.c -c -o memory_test.o

value_test: $(OBJT) $(OBJX) value_test.o
	$(CC) $(OBJT) $(OBJX) value_test.o -o value_test

value_test.o: $(INCT) value_test.c 
	$(CC) $(CFLAGS) value_test.c -c -o value_test.o

skip_list_test: $(OBJT) $(OBJX) skip_list_test.o
	$(CC) $(OBJT) $(OBJX) skip_list_test.o -o skip_list_test

skip_list_test.o: $(INCT) skip_list_test.c 
	$(CC) $(CFLAGS) skip_list_test.c -c -o skip_list_test.o

hash_map_test: $(OBJT) $(OBJX) hash_map_test.o
	$(CC) $(OBJT) $(OBJX) hash_map_test.o -o hash_map_test

hash_map_test.o: $(INCT) hash_map_test.c 
	$(CC) $(CFLAGS) hash_map_test.c -c -o hash_map_test.o

dynamic_array_test: $(OBJT) $(OBJX) dynamic_array_test.o
	$(CC) $(OBJT) $(OBJX) dynamic_array_test.o -o dynamic_array_test

dynamic_array_test.o: $(INCT) dynamic_array_test.c
	$(CC) $(CFLAGS) dynamic_array_test.c -c -o dynamic_array_test.o

closure_test: $(OBJT) $(OBJX) closure_test.o
	$(CC) $(OBJT) $(OBJX) closure_test.o -o closure_test

closure_test.o: $(INCT) closure_test.c
	$(CC) $(CFLAGS) closure_test.c -c -o closure_test.o

call_test: $(OBJT) $(OBJX) call_test.o 
	$(CC) $(OBJT) $(OBJX) call_test.o -o call_test

call_test.o: $(INCT) libc.h call_test.c 
	$(CC) $(CFLAGS) call_test.c -c -o call_test.o

encoding_test: $(OBJT) $(OBJX) encoding_test.o
	$(CC) $(OBJT) $(OBJX) encoding_test.o -o encoding_test

encoding_test.o: $(INCT) encoding_test.c encoding.h assembly.h 
	$(CC) $(CFLAGS) encoding_test.c -c -o encoding_test.o

compiler_test: $(OBJT) $(OBJX) compiler_test.o
	$(CC) $(OBJT) compiler_test.o -o compiler_test

compiler_test.o: $(INCT) compiler_test.c compiler.h lex.h
	$(CC) $(CFLAGS) compiler_test.c -c -o compiler_test.o

lex_test: $(OBJT) $(OBJX) lex_test.o
	$(CC) $(OBJT) $(OBJX) lex_test.o -o lex_test 

lex_test.o: lex_test.c lex.h
	$(CC) $(CFLAGS) lex_test.c -c -o lex_test.o

print_test: $(OBJT) $(OBJX) print_test.o print.o 
	$(CC) $(OBJT) print_test.o print.o -o print_test

print_test.o: print_test.c print.h
	$(CC) $(CFLAGS) print_test.c -c -o print_test.o

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

#-------------------------------------------------------------------------------
# 3rd party libs rules:
$(LIB_REGEX):
	cd 3rd && make regex

#-------------------------------------------------------------------------------
# Objects rules:
disassembly.o: disassembly.c disassembly.h assembly.h value.h value_inl.h
	$(CC) $(CFLAGS) disassembly.c -c -o disassembly.o

compiler.o: $(INCS) compiler.c compiler.h lex.h
	$(CC) $(CFLAGS) compiler.c -c -o compiler.o

lex.o: lex.c lex.h
	$(CC) $(CFLAGS) lex.c -c -o lex.o

state.o: state.c state.h value.h value_inl.h memory.h
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

libos.o: $(INCS) libos_posix.c
	$(CC) $(CFLAGS) libos_posix.c -c -o libos.o

libtest.o: $(INCS) print.h libc.h libtest.h libtest.c
	$(CC) $(CFLAGS) libtest.c -c -o libtest.o

pickle.o: $(INCS) pickle.h zstream.h
	$(CC) $(CFLAGS) pickle.c -c -o pickle.o

print.o: print.h print_posix.c
	$(CC) $(CFLAGS) print_posix.c -c -o print.o

tostring.o: $(INCS) tostring.c tostring.h
	$(CC) $(CFLAGS) tostring.c -c -o tostring.o

string.o: $(INCS) string.c
	$(CC) $(CFLAGS) string.c -c -o string.o

encoding.o: $(INCS) encoding.c encoding.h
	$(CC) $(CFLAGS) encoding.c -c -o encoding.o

value.o: value.c value.h value_inl.h state.h
	$(CC) $(CFLAGS) value.c -c -o value.o

memory.o: memory.c memory.h state.h
	$(CC) $(CFLAGS) memory.c -c -o memory.o

print_posix.o: print_posix.c print.h
	$(CC) $(CFLAGS) print_posix.c -c -o print_posix.o

.PHONY: clean
clean:
	rm -f *.o *_test y.tab.c y.tab.h lex.yy.c ymd_main
	cd 3rd && make clean

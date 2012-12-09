include ../../config.mk
CFLAGS=$(DFLAGS) -I. -DPOSIX_MISTAKE -DREDEBUG

libregex.a: regcomp.o regexec.o regerror.o regfree.o
	ar crv libregex.a regcomp.o regexec.o regerror.o regfree.o

regcomp.o: regcomp.c
	$(CC) $(CFLAGS) regcomp.c -c -o regcomp.o

regexec.o: regexec.c
	$(CC) $(CFLAGS) regexec.c -c -o regexec.o

regerror.o: regerror.c
	$(CC) $(CFLAGS) regerror.c -c -o regerror.o

regfree.o: regfree.c
	$(CC) $(CFLAGS) regfree.c -c -o regfree.o

clean:
	rm -f *.o *.a

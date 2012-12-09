#!/usr/bin/python

import sys
import re

REGEX_CASE     = 'static int (test_\w+)[ ]?\(([\w\d_ ]+)\*[\w\d_ ]+\)'
REGEX_SETUP    = 'static [\w\d_ ]+ \*setup[ ]?\(\)'
REGEX_TEARDOWN = 'static void teardown[]?\([\w\d_ ]+\*[\w\d_ ]+\)'

DEF_HEAD = '''
#include "yut.h"

#define SETUP(p)    ((yut_setup_t)p)
#define TEARDOWN(p) ((yut_teardown_t)p)
#define CASE(p)     ((yut_case_t)p)

'''

DEF_TAIL = '''

#undef CASE
#undef TEARDOWN
#undef SETUP

'''
class Scanner:
	def __init__(self, file_name, out_dir):
		self.file_name = file_name
		self.out_dir = out_dir
		self.test_case = []
		self.test_func = []

	def scan(self):
		# Scan file for find all test case
		inf = open(self.file_name, 'r')
		for line in inf:
			rv = re.match(REGEX_CASE, line)
			if rv:
				self.test_func.append(rv.group(0))
				self.test_case.append(rv.group(1))
				self.test_args = rv.group(2)
			rv = re.match(REGEX_SETUP, line)
			if rv:
				self.test_setup = rv.group(0)
			rv = re.match(REGEX_TEARDOWN, line)
			if rv:
				self.test_teardown = rv.group(0)
		inf.close()

	def generate(self):
		rv = re.match('([\w\d_-]*)\.c', self.file_name)
		if not rv:
			raise Exception('Bad file name: ' + self.file_name)
		self.test_name = rv.group(1)
		def_name = self.out_dir + '/' + self.test_name + '.def'
		self.do_generate(open(def_name, 'w'))

	def do_generate(self, outf):
		# Define protected macros:
		outf.write('#ifndef UNIQUE_INCLUDE_' \
				+ self.test_name.upper() + '_DEF\n')
		outf.write('#define UNIQUE_INCLUDE_' \
				+ self.test_name.upper() + '_DEF\n')
		outf.write(DEF_HEAD)

		# Forward declare struct and functions:
		if not self.test_args.startswith('void'):
			outf.write(self.test_args + ';\n')
		if hasattr(self, 'test_setup'):
			outf.write(self.test_setup + ';\n')
		if hasattr(self, 'test_teardown'):
			outf.write(self.test_teardown + ';\n')
		for func in self.test_func:
			outf.write(func + ';\n')
		outf.write('\n')

		# Define test case struct:
		outf.write('const struct yut_case_def ' \
				+ self.test_name \
				+ ' = {\n' \
				+ '\t\"' + self.test_name + '\",\n')

		# Setup and teardown define:
		if hasattr(self, 'test_setup'):
			outf.write('\tSETUP(setup),\n')
		else:
			outf.write('\tNULL,\n')
		if hasattr(self, 'test_teardown'):
			outf.write('\tTEARDOWN(teardown),\n')
		else:
			outf.write('\tNULL,\n')
		outf.write('\t{\n')

		# Test cases define:
		for caze in self.test_case:
			outf.write('\t\t{ \"' + caze[5:] + '\", ')
			outf.write('CASE(' + caze + '), },\n')

		# Finalize it!
		outf.write('\t\t{ NULL, NULL, },\n')
		outf.write('\t},\n};\n\n')
		outf.write('#ifndef YUT_ALL_TEST\n')
		outf.write('const struct yut_case_def *yut_intl_test[] = {' \
				+ ' &' + self.test_name + ', NULL, ' \
				+ '};\n')
		outf.write('#endif\n')
		outf.write(DEF_TAIL)
		outf.write('#endif // UNIQUE_INCLUDE_' + \
				self.test_name.upper() + '_DEF\n\n')
		outf.close()

if __name__ == '__main__':
	if len(sys.argv) < 3:
		scanner = Scanner(sys.argv[1], '.')
	else:
		scanner = Scanner(sys.argv[1], sys.argv[2])
	scanner.scan()
	print scanner.test_case
	scanner.generate()

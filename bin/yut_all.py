#!/usr/bin/python
import sys

DEF_HEAD = '''#ifndef UNIQUE_INCLUDE_YUT_ALL_TEST_DEF
#define UNIQUE_INCLUDE_YUT_ALL_TEST_DEF

#include "yut.h"

#if defined(YUT_ALL_TEST)

'''

DEF_TAIL = '''
#endif // defined(YUT_ALL_TEST)

#endif // UNIQUE_INCLUDE_YUT_ALL_TEST_DEF

'''

class FusedTest:
	def __init__(self, test, out_dir):
		self.test = test
		self.file_name = out_dir + '/all_test.def'

	def generate(self):
		outf = open(self.file_name, 'w')
		outf.write(DEF_HEAD)

		# Forward declare test case
		for elem in self.test:
			outf.write('extern const struct yut_case_def ' \
					+ elem + '_test;\n')

		# Define all test cases
		outf.write('\n')
		outf.write('const struct yut_case_def *yut_intl_test[] = {\n')
		for elem in self.test:
			outf.write('\t&' + elem + '_test,\n')
		outf.write('\tNULL,\n')
		outf.write('};\n')
		outf.write(DEF_TAIL)

if __name__ == '__main__':
	test = sys.argv[1].split(',')
	fused = FusedTest(test, sys.argv[2])
	fused.generate()


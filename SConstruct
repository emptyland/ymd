import os
from SCons.Script import *

conf = {
	'CC': 'clang',
	'CXX': 'clang++',
	'CCFLAGS': '-Wall -Wextra ',
	'profile': False
}

# Parse arguments
if ARGUMENTS.get('profile', 0):
	conf['CCFLAGS'] = conf['CCFLAGS'] + '-O2 -g3 -DNDEBUG'
	conf['profile'] = True
else:
	conf['CCFLAGS'] = conf['CCFLAGS'] + '-O0 -g3'
	conf['profile'] = False

# Use environment vairable for configuration
if os.environ.has_key('CC'):
	conf['CC'] = os.environ['CC']
if os.environ.has_key('CXX'):
	conf['CXX'] = os.environ['CXX']
if os.environ.has_key('CCFLAGS'):
	conf['CCFLAGS'] = os.environ['CCFLAGS']

# Only clang compiler has color output
if conf['CC'] == 'clang':
	conf['CCFLAGS'] = conf['CCFLAGS'] + ' -fcolor-diagnostics'

env = Environment(CC = conf['CC'],
	CXX = conf['CXX'],
	CCFLAGS = conf['CCFLAGS'],
    LIBPATH = '#src third_party/pcre'.split(),
	platform  = 'posix')
env.profile = conf['profile']

Export('env')
SConscript('src/SConscript')
SConscript('src/third_party/pcre/SConscript')

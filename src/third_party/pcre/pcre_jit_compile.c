/*************************************************
*      Perl-Compatible Regular Expressions       *
*************************************************/

/* PCRE is a library of functions to support regular expressions whose syntax
and semantics are as close as possible to those of the Perl 5 language.

                       Written by Philip Hazel
           Copyright (c) 1997-2012 University of Cambridge

  The machine code generator part (this module) was written by Zoltan Herczeg
                      Copyright (c) 2010-2012

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pcre_internal.h"

#if defined SUPPORT_JIT

/* All-in-one: Since we use the JIT compiler only from here,
we just include it. This way we don't need to touch the build
system files. */

#define SLJIT_MALLOC(size) (PUBL(malloc))(size)
#define SLJIT_FREE(ptr) (PUBL(free))(ptr)
#define SLJIT_CONFIG_AUTO 1
#define SLJIT_CONFIG_STATIC 1
#define SLJIT_VERBOSE 0
#define SLJIT_DEBUG 0

#include "sljit/sljitLir.c"

#if defined SLJIT_CONFIG_UNSUPPORTED && SLJIT_CONFIG_UNSUPPORTED
#error Unsupported architecture
#endif

/* Allocate memory for the regex stack on the real machine stack.
Fast, but limited size. */
#define MACHINE_STACK_SIZE 32768

/* Growth rate for stack allocated by the OS. Should be the multiply
of page size. */
#define STACK_GROWTH_RATE 8192

/* Enable to check that the allocation could destroy temporaries. */
#if defined SLJIT_DEBUG && SLJIT_DEBUG
#define DESTROY_REGISTERS 1
#endif

/*
Short summary about the backtracking mechanism empolyed by the jit code generator:

The code generator follows the recursive nature of the PERL compatible regular
expressions. The basic blocks of regular expressions are condition checkers
whose execute different commands depending on the result of the condition check.
The relationship between the operators can be horizontal (concatenation) and
vertical (sub-expression) (See struct backtrack_common for more details).

  'ab' - 'a' and 'b' regexps are concatenated
  'a+' - 'a' is the sub-expression of the '+' operator

The condition checkers are boolean (true/false) checkers. Machine code is generated
for the checker itself and for the actions depending on the result of the checker.
The 'true' case is called as the matching path (expected path), and the other is called as
the 'backtrack' path. Branch instructions are expesive for all CPUs, so we avoid taken
branches on the matching path.

 Greedy star operator (*) :
   Matching path: match happens.
   Backtrack path: match failed.
 Non-greedy star operator (*?) :
   Matching path: no need to perform a match.
   Backtrack path: match is required.

The following example shows how the code generated for a capturing bracket
with two alternatives. Let A, B, C, D are arbirary regular expressions, and
we have the following regular expression:

   A(B|C)D

The generated code will be the following:

 A matching path
 '(' matching path (pushing arguments to the stack)
 B matching path
 ')' matching path (pushing arguments to the stack)
 D matching path
 return with successful match

 D backtrack path
 ')' backtrack path (If we arrived from "C" jump to the backtrack of "C")
 B backtrack path
 C expected path
 jump to D matching path
 C backtrack path
 A backtrack path

 Notice, that the order of backtrack code paths are the opposite of the fast
 code paths. In this way the topmost value on the stack is always belong
 to the current backtrack code path. The backtrack path must check
 whether there is a next alternative. If so, it needs to jump back to
 the matching path eventually. Otherwise it needs to clear out its own stack
 frame and continue the execution on the backtrack code paths.
*/

/*
Saved stack frames:

Atomic blocks and asserts require reloading the values of private data
when the backtrack mechanism performed. Because of OP_RECURSE, the data
are not necessarly known in compile time, thus we need a dynamic restore
mechanism.

The stack frames are stored in a chain list, and have the following format:
([ capturing bracket offset ][ start value ][ end value ])+ ... [ 0 ] [ previous head ]

Thus we can restore the private data to a particular point in the stack.
*/

typedef struct jit_arguments {
  /* Pointers first. */
  struct sljit_stack *stack;
  const pcre_uchar *str;
  const pcre_uchar *begin;
  const pcre_uchar *end;
  int *offsets;
  pcre_uchar *uchar_ptr;
  pcre_uchar *mark_ptr;
  /* Everything else after. */
  int offsetcount;
  int calllimit;
  pcre_uint8 notbol;
  pcre_uint8 noteol;
  pcre_uint8 notempty;
  pcre_uint8 notempty_atstart;
} jit_arguments;

typedef struct executable_functions {
  void *executable_funcs[JIT_NUMBER_OF_COMPILE_MODES];
  PUBL(jit_callback) callback;
  void *userdata;
  pcre_uint32 top_bracket;
  sljit_uw executable_sizes[JIT_NUMBER_OF_COMPILE_MODES];
} executable_functions;

typedef struct jump_list {
  struct sljit_jump *jump;
  struct jump_list *next;
} jump_list;

enum stub_types { stack_alloc };

typedef struct stub_list {
  enum stub_types type;
  int data;
  struct sljit_jump *start;
  struct sljit_label *quit;
  struct stub_list *next;
} stub_list;

typedef int (SLJIT_CALL *jit_function)(jit_arguments *args);

/* The following structure is the key data type for the recursive
code generator. It is allocated by compile_matchingpath, and contains
the aguments for compile_backtrackingpath. Must be the first member
of its descendants. */
typedef struct backtrack_common {
  /* Concatenation stack. */
  struct backtrack_common *prev;
  jump_list *nextbacktracks;
  /* Internal stack (for component operators). */
  struct backtrack_common *top;
  jump_list *topbacktracks;
  /* Opcode pointer. */
  pcre_uchar *cc;
} backtrack_common;

typedef struct assert_backtrack {
  backtrack_common common;
  jump_list *condfailed;
  /* Less than 0 (-1) if a frame is not needed. */
  int framesize;
  /* Points to our private memory word on the stack. */
  int private_data_ptr;
  /* For iterators. */
  struct sljit_label *matchingpath;
} assert_backtrack;

typedef struct bracket_backtrack {
  backtrack_common common;
  /* Where to coninue if an alternative is successfully matched. */
  struct sljit_label *alternative_matchingpath;
  /* For rmin and rmax iterators. */
  struct sljit_label *recursive_matchingpath;
  /* For greedy ? operator. */
  struct sljit_label *zero_matchingpath;
  /* Contains the branches of a failed condition. */
  union {
    /* Both for OP_COND, OP_SCOND. */
    jump_list *condfailed;
    assert_backtrack *assert;
    /* For OP_ONCE. -1 if not needed. */
    int framesize;
  } u;
  /* Points to our private memory word on the stack. */
  int private_data_ptr;
} bracket_backtrack;

typedef struct bracketpos_backtrack {
  backtrack_common common;
  /* Points to our private memory word on the stack. */
  int private_data_ptr;
  /* Reverting stack is needed. */
  int framesize;
  /* Allocated stack size. */
  int stacksize;
} bracketpos_backtrack;

typedef struct braminzero_backtrack {
  backtrack_common common;
  struct sljit_label *matchingpath;
} braminzero_backtrack;

typedef struct iterator_backtrack {
  backtrack_common common;
  /* Next iteration. */
  struct sljit_label *matchingpath;
} iterator_backtrack;

typedef struct recurse_entry {
  struct recurse_entry *next;
  /* Contains the function entry. */
  struct sljit_label *entry;
  /* Collects the calls until the function is not created. */
  jump_list *calls;
  /* Points to the starting opcode. */
  int start;
} recurse_entry;

typedef struct recurse_backtrack {
  backtrack_common common;
} recurse_backtrack;

#define MAX_RANGE_SIZE 6

typedef struct compiler_common {
  struct sljit_compiler *compiler;
  pcre_uchar *start;

  /* Maps private data offset to each opcode. */
  int *private_data_ptrs;
  /* Tells whether the capturing bracket is optimized. */
  pcre_uint8 *optimized_cbracket;
  /* Starting offset of private data for capturing brackets. */
  int cbraptr;
  /* OVector starting point. Must be divisible by 2. */
  int ovector_start;
  /* Last known position of the requested byte. */
  int req_char_ptr;
  /* Head of the last recursion. */
  int recursive_head;
  /* First inspected character for partial matching. */
  int start_used_ptr;
  /* Starting pointer for partial soft matches. */
  int hit_start;
  /* End pointer of the first line. */
  int first_line_end;
  /* Points to the marked string. */
  int mark_ptr;

  /* Flipped and lower case tables. */
  const pcre_uint8 *fcc;
  sljit_sw lcc;
  /* Mode can be PCRE_STUDY_JIT_COMPILE and others. */
  int mode;
  /* Newline control. */
  int nltype;
  int newline;
  int bsr_nltype;
  /* Dollar endonly. */
  int endonly;
  BOOL has_set_som;
  /* Tables. */
  sljit_sw ctypes;
  int digits[2 + MAX_RANGE_SIZE];
  /* Named capturing brackets. */
  sljit_uw name_table;
  sljit_sw name_count;
  sljit_sw name_entry_size;

  /* Labels and jump lists. */
  struct sljit_label *partialmatchlabel;
  struct sljit_label *quitlabel;
  struct sljit_label *acceptlabel;
  stub_list *stubs;
  recurse_entry *entries;
  recurse_entry *currententry;
  jump_list *partialmatch;
  jump_list *quit;
  jump_list *accept;
  jump_list *calllimit;
  jump_list *stackalloc;
  jump_list *revertframes;
  jump_list *wordboundary;
  jump_list *anynewline;
  jump_list *hspace;
  jump_list *vspace;
  jump_list *casefulcmp;
  jump_list *caselesscmp;
  BOOL jscript_compat;
#ifdef SUPPORT_UTF
  BOOL utf;
#ifdef SUPPORT_UCP
  BOOL use_ucp;
#endif
#ifndef COMPILE_PCRE32
  jump_list *utfreadchar;
#endif
#ifdef COMPILE_PCRE8
  jump_list *utfreadtype8;
#endif
#endif /* SUPPORT_UTF */
#ifdef SUPPORT_UCP
  jump_list *getucd;
#endif
} compiler_common;

/* For byte_sequence_compare. */

typedef struct compare_context {
  int length;
  int sourcereg;
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED
  int ucharptr;
  union {
    sljit_si asint;
    sljit_uh asushort;
#if defined COMPILE_PCRE8
    sljit_ub asbyte;
    sljit_ub asuchars[4];
#elif defined COMPILE_PCRE16
    sljit_uh asuchars[2];
#elif defined COMPILE_PCRE32
    sljit_ui asuchars[1];
#endif
  } c;
  union {
    sljit_si asint;
    sljit_uh asushort;
#if defined COMPILE_PCRE8
    sljit_ub asbyte;
    sljit_ub asuchars[4];
#elif defined COMPILE_PCRE16
    sljit_uh asuchars[2];
#elif defined COMPILE_PCRE32
    sljit_ui asuchars[1];
#endif
  } oc;
#endif
} compare_context;

enum {
  frame_end = 0,
  frame_setstrbegin = -1,
  frame_setmark = -2
};

/* Undefine sljit macros. */
#undef CMP

/* Used for accessing the elements of the stack. */
#define STACK(i)      ((-(i) - 1) * (int)sizeof(sljit_sw))

#define TMP1          SLJIT_SCRATCH_REG1
#define TMP2          SLJIT_SCRATCH_REG3
#define TMP3          SLJIT_TEMPORARY_EREG2
#define STR_PTR       SLJIT_SAVED_REG1
#define STR_END       SLJIT_SAVED_REG2
#define STACK_TOP     SLJIT_SCRATCH_REG2
#define STACK_LIMIT   SLJIT_SAVED_REG3
#define ARGUMENTS     SLJIT_SAVED_EREG1
#define CALL_COUNT    SLJIT_SAVED_EREG2
#define RETURN_ADDR   SLJIT_TEMPORARY_EREG1

/* Local space layout. */
/* These two locals can be used by the current opcode. */
#define LOCALS0          (0 * sizeof(sljit_sw))
#define LOCALS1          (1 * sizeof(sljit_sw))
/* Two local variables for possessive quantifiers (char1 cannot use them). */
#define POSSESSIVE0      (2 * sizeof(sljit_sw))
#define POSSESSIVE1      (3 * sizeof(sljit_sw))
/* Max limit of recursions. */
#define CALL_LIMIT       (4 * sizeof(sljit_sw))
/* The output vector is stored on the stack, and contains pointers
to characters. The vector data is divided into two groups: the first
group contains the start / end character pointers, and the second is
the start pointers when the end of the capturing group has not yet reached. */
#define OVECTOR_START    (common->ovector_start)
#define OVECTOR(i)       (OVECTOR_START + (i) * sizeof(sljit_sw))
#define OVECTOR_PRIV(i)  (common->cbraptr + (i) * sizeof(sljit_sw))
#define PRIVATE_DATA(cc) (common->private_data_ptrs[(cc) - common->start])

#if defined COMPILE_PCRE8
#define MOV_UCHAR  SLJIT_MOV_UB
#define MOVU_UCHAR SLJIT_MOVU_UB
#elif defined COMPILE_PCRE16
#define MOV_UCHAR  SLJIT_MOV_UH
#define MOVU_UCHAR SLJIT_MOVU_UH
#elif defined COMPILE_PCRE32
#define MOV_UCHAR  SLJIT_MOV_UI
#define MOVU_UCHAR SLJIT_MOVU_UI
#else
#error Unsupported compiling mode
#endif

/* Shortcuts. */
#define DEFINE_COMPILER \
  struct sljit_compiler *compiler = common->compiler
#define OP1(op, dst, dstw, src, srcw) \
  sljit_emit_op1(compiler, (op), (dst), (dstw), (src), (srcw))
#define OP2(op, dst, dstw, src1, src1w, src2, src2w) \
  sljit_emit_op2(compiler, (op), (dst), (dstw), (src1), (src1w), (src2), (src2w))
#define LABEL() \
  sljit_emit_label(compiler)
#define JUMP(type) \
  sljit_emit_jump(compiler, (type))
#define JUMPTO(type, label) \
  sljit_set_label(sljit_emit_jump(compiler, (type)), (label))
#define JUMPHERE(jump) \
  sljit_set_label((jump), sljit_emit_label(compiler))
#define CMP(type, src1, src1w, src2, src2w) \
  sljit_emit_cmp(compiler, (type), (src1), (src1w), (src2), (src2w))
#define CMPTO(type, src1, src1w, src2, src2w, label) \
  sljit_set_label(sljit_emit_cmp(compiler, (type), (src1), (src1w), (src2), (src2w)), (label))
#define OP_FLAGS(op, dst, dstw, src, srcw, type) \
  sljit_emit_op_flags(compiler, (op), (dst), (dstw), (src), (srcw), (type))
#define GET_LOCAL_BASE(dst, dstw, offset) \
  sljit_get_local_base(compiler, (dst), (dstw), (offset))

static pcre_uchar* bracketend(pcre_uchar* cc)
{
SLJIT_ASSERT((*cc >= OP_ASSERT && *cc <= OP_ASSERTBACK_NOT) || (*cc >= OP_ONCE && *cc <= OP_SCOND));
do cc += GET(cc, 1); while (*cc == OP_ALT);
SLJIT_ASSERT(*cc >= OP_KET && *cc <= OP_KETRPOS);
cc += 1 + LINK_SIZE;
return cc;
}

/* Functions whose might need modification for all new supported opcodes:
 next_opcode
 get_private_data_length
 set_private_data_ptrs
 get_framesize
 init_frame
 get_private_data_length_for_copy
 copy_private_data
 compile_matchingpath
 compile_backtrackingpath
*/

static pcre_uchar *next_opcode(compiler_common *common, pcre_uchar *cc)
{
SLJIT_UNUSED_ARG(common);
switch(*cc)
  {
  case OP_SOD:
  case OP_SOM:
  case OP_SET_SOM:
  case OP_NOT_WORD_BOUNDARY:
  case OP_WORD_BOUNDARY:
  case OP_NOT_DIGIT:
  case OP_DIGIT:
  case OP_NOT_WHITESPACE:
  case OP_WHITESPACE:
  case OP_NOT_WORDCHAR:
  case OP_WORDCHAR:
  case OP_ANY:
  case OP_ALLANY:
  case OP_ANYNL:
  case OP_NOT_HSPACE:
  case OP_HSPACE:
  case OP_NOT_VSPACE:
  case OP_VSPACE:
  case OP_EXTUNI:
  case OP_EODN:
  case OP_EOD:
  case OP_CIRC:
  case OP_CIRCM:
  case OP_DOLL:
  case OP_DOLLM:
  case OP_TYPESTAR:
  case OP_TYPEMINSTAR:
  case OP_TYPEPLUS:
  case OP_TYPEMINPLUS:
  case OP_TYPEQUERY:
  case OP_TYPEMINQUERY:
  case OP_TYPEPOSSTAR:
  case OP_TYPEPOSPLUS:
  case OP_TYPEPOSQUERY:
  case OP_CRSTAR:
  case OP_CRMINSTAR:
  case OP_CRPLUS:
  case OP_CRMINPLUS:
  case OP_CRQUERY:
  case OP_CRMINQUERY:
  case OP_DEF:
  case OP_BRAZERO:
  case OP_BRAMINZERO:
  case OP_BRAPOSZERO:
  case OP_COMMIT:
  case OP_FAIL:
  case OP_ACCEPT:
  case OP_ASSERT_ACCEPT:
  case OP_SKIPZERO:
  return cc + 1;

  case OP_ANYBYTE:
#ifdef SUPPORT_UTF
  if (common->utf) return NULL;
#endif
  return cc + 1;

  case OP_CHAR:
  case OP_CHARI:
  case OP_NOT:
  case OP_NOTI:
  case OP_STAR:
  case OP_MINSTAR:
  case OP_PLUS:
  case OP_MINPLUS:
  case OP_QUERY:
  case OP_MINQUERY:
  case OP_POSSTAR:
  case OP_POSPLUS:
  case OP_POSQUERY:
  case OP_STARI:
  case OP_MINSTARI:
  case OP_PLUSI:
  case OP_MINPLUSI:
  case OP_QUERYI:
  case OP_MINQUERYI:
  case OP_POSSTARI:
  case OP_POSPLUSI:
  case OP_POSQUERYI:
  case OP_NOTSTAR:
  case OP_NOTMINSTAR:
  case OP_NOTPLUS:
  case OP_NOTMINPLUS:
  case OP_NOTQUERY:
  case OP_NOTMINQUERY:
  case OP_NOTPOSSTAR:
  case OP_NOTPOSPLUS:
  case OP_NOTPOSQUERY:
  case OP_NOTSTARI:
  case OP_NOTMINSTARI:
  case OP_NOTPLUSI:
  case OP_NOTMINPLUSI:
  case OP_NOTQUERYI:
  case OP_NOTMINQUERYI:
  case OP_NOTPOSSTARI:
  case OP_NOTPOSPLUSI:
  case OP_NOTPOSQUERYI:
  cc += 2;
#ifdef SUPPORT_UTF
  if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
  return cc;

  case OP_UPTO:
  case OP_MINUPTO:
  case OP_EXACT:
  case OP_POSUPTO:
  case OP_UPTOI:
  case OP_MINUPTOI:
  case OP_EXACTI:
  case OP_POSUPTOI:
  case OP_NOTUPTO:
  case OP_NOTMINUPTO:
  case OP_NOTEXACT:
  case OP_NOTPOSUPTO:
  case OP_NOTUPTOI:
  case OP_NOTMINUPTOI:
  case OP_NOTEXACTI:
  case OP_NOTPOSUPTOI:
  cc += 2 + IMM2_SIZE;
#ifdef SUPPORT_UTF
  if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
  return cc;

  case OP_NOTPROP:
  case OP_PROP:
  return cc + 1 + 2;

  case OP_TYPEUPTO:
  case OP_TYPEMINUPTO:
  case OP_TYPEEXACT:
  case OP_TYPEPOSUPTO:
  case OP_REF:
  case OP_REFI:
  case OP_CREF:
  case OP_NCREF:
  case OP_RREF:
  case OP_NRREF:
  case OP_CLOSE:
  cc += 1 + IMM2_SIZE;
  return cc;

  case OP_CRRANGE:
  case OP_CRMINRANGE:
  return cc + 1 + 2 * IMM2_SIZE;

  case OP_CLASS:
  case OP_NCLASS:
  return cc + 1 + 32 / sizeof(pcre_uchar);

#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
  case OP_XCLASS:
  return cc + GET(cc, 1);
#endif

  case OP_RECURSE:
  case OP_ASSERT:
  case OP_ASSERT_NOT:
  case OP_ASSERTBACK:
  case OP_ASSERTBACK_NOT:
  case OP_REVERSE:
  case OP_ONCE:
  case OP_ONCE_NC:
  case OP_BRA:
  case OP_BRAPOS:
  case OP_COND:
  case OP_SBRA:
  case OP_SBRAPOS:
  case OP_SCOND:
  case OP_ALT:
  case OP_KET:
  case OP_KETRMAX:
  case OP_KETRMIN:
  case OP_KETRPOS:
  return cc + 1 + LINK_SIZE;

  case OP_CBRA:
  case OP_CBRAPOS:
  case OP_SCBRA:
  case OP_SCBRAPOS:
  return cc + 1 + LINK_SIZE + IMM2_SIZE;

  case OP_MARK:
  return cc + 1 + 2 + cc[1];

  default:
  return NULL;
  }
}

#define CASE_ITERATOR_PRIVATE_DATA_1 \
    case OP_MINSTAR: \
    case OP_MINPLUS: \
    case OP_QUERY: \
    case OP_MINQUERY: \
    case OP_MINSTARI: \
    case OP_MINPLUSI: \
    case OP_QUERYI: \
    case OP_MINQUERYI: \
    case OP_NOTMINSTAR: \
    case OP_NOTMINPLUS: \
    case OP_NOTQUERY: \
    case OP_NOTMINQUERY: \
    case OP_NOTMINSTARI: \
    case OP_NOTMINPLUSI: \
    case OP_NOTQUERYI: \
    case OP_NOTMINQUERYI:

#define CASE_ITERATOR_PRIVATE_DATA_2A \
    case OP_STAR: \
    case OP_PLUS: \
    case OP_STARI: \
    case OP_PLUSI: \
    case OP_NOTSTAR: \
    case OP_NOTPLUS: \
    case OP_NOTSTARI: \
    case OP_NOTPLUSI:

#define CASE_ITERATOR_PRIVATE_DATA_2B \
    case OP_UPTO: \
    case OP_MINUPTO: \
    case OP_UPTOI: \
    case OP_MINUPTOI: \
    case OP_NOTUPTO: \
    case OP_NOTMINUPTO: \
    case OP_NOTUPTOI: \
    case OP_NOTMINUPTOI:

#define CASE_ITERATOR_TYPE_PRIVATE_DATA_1 \
    case OP_TYPEMINSTAR: \
    case OP_TYPEMINPLUS: \
    case OP_TYPEQUERY: \
    case OP_TYPEMINQUERY:

#define CASE_ITERATOR_TYPE_PRIVATE_DATA_2A \
    case OP_TYPESTAR: \
    case OP_TYPEPLUS:

#define CASE_ITERATOR_TYPE_PRIVATE_DATA_2B \
    case OP_TYPEUPTO: \
    case OP_TYPEMINUPTO:

static int get_class_iterator_size(pcre_uchar *cc)
{
switch(*cc)
  {
  case OP_CRSTAR:
  case OP_CRPLUS:
  return 2;

  case OP_CRMINSTAR:
  case OP_CRMINPLUS:
  case OP_CRQUERY:
  case OP_CRMINQUERY:
  return 1;

  case OP_CRRANGE:
  case OP_CRMINRANGE:
  if (GET2(cc, 1) == GET2(cc, 1 + IMM2_SIZE))
    return 0;
  return 2;

  default:
  return 0;
  }
}

static int get_private_data_length(compiler_common *common, pcre_uchar *cc, pcre_uchar *ccend)
{
int private_data_length = 0;
pcre_uchar *alternative;
pcre_uchar *name;
pcre_uchar *end = NULL;
int space, size, i;
pcre_uint32 bracketlen;

/* Calculate important variables (like stack size) and checks whether all opcodes are supported. */
while (cc < ccend)
  {
  space = 0;
  size = 0;
  bracketlen = 0;
  switch(*cc)
    {
    case OP_SET_SOM:
    common->has_set_som = TRUE;
    cc += 1;
    break;

    case OP_REF:
    case OP_REFI:
    common->optimized_cbracket[GET2(cc, 1)] = 0;
    cc += 1 + IMM2_SIZE;
    break;

    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    case OP_ONCE:
    case OP_ONCE_NC:
    case OP_BRAPOS:
    case OP_SBRA:
    case OP_SBRAPOS:
    private_data_length += sizeof(sljit_sw);
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_CBRAPOS:
    case OP_SCBRAPOS:
    private_data_length += sizeof(sljit_sw);
    common->optimized_cbracket[GET2(cc, 1 + LINK_SIZE)] = 0;
    bracketlen = 1 + LINK_SIZE + IMM2_SIZE;
    break;

    case OP_COND:
    case OP_SCOND:
    bracketlen = cc[1 + LINK_SIZE];
    if (bracketlen == OP_CREF)
      {
      bracketlen = GET2(cc, 1 + LINK_SIZE + 1);
      common->optimized_cbracket[bracketlen] = 0;
      }
    else if (bracketlen == OP_NCREF)
      {
      bracketlen = GET2(cc, 1 + LINK_SIZE + 1);
      name = (pcre_uchar *)common->name_table;
      alternative = name;
      for (i = 0; i < common->name_count; i++)
        {
        if (GET2(name, 0) == bracketlen) break;
        name += common->name_entry_size;
        }
      SLJIT_ASSERT(i != common->name_count);

      for (i = 0; i < common->name_count; i++)
        {
        if (STRCMP_UC_UC(alternative + IMM2_SIZE, name + IMM2_SIZE) == 0)
          common->optimized_cbracket[GET2(alternative, 0)] = 0;
        alternative += common->name_entry_size;
        }
      }

    if (*cc == OP_COND)
      {
      /* Might be a hidden SCOND. */
      alternative = cc + GET(cc, 1);
      if (*alternative == OP_KETRMAX || *alternative == OP_KETRMIN)
        private_data_length += sizeof(sljit_sw);
      }
    else
      private_data_length += sizeof(sljit_sw);
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_BRA:
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_CBRA:
    case OP_SCBRA:
    bracketlen = 1 + LINK_SIZE + IMM2_SIZE;
    break;

    CASE_ITERATOR_PRIVATE_DATA_1
    space = 1;
    size = -2;
    break;

    CASE_ITERATOR_PRIVATE_DATA_2A
    space = 2;
    size = -2;
    break;

    CASE_ITERATOR_PRIVATE_DATA_2B
    space = 2;
    size = -(2 + IMM2_SIZE);
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_1
    space = 1;
    size = 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2A
    if (cc[1] != OP_ANYNL && cc[1] != OP_EXTUNI)
      space = 2;
    size = 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2B
    if (cc[1 + IMM2_SIZE] != OP_ANYNL && cc[1 + IMM2_SIZE] != OP_EXTUNI)
      space = 2;
    size = 1 + IMM2_SIZE;
    break;

    case OP_CLASS:
    case OP_NCLASS:
    size += 1 + 32 / sizeof(pcre_uchar);
    space = get_class_iterator_size(cc + size);
    break;

#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
    case OP_XCLASS:
    size = GET(cc, 1);
    space = get_class_iterator_size(cc + size);
    break;
#endif

    case OP_RECURSE:
    /* Set its value only once. */
    if (common->recursive_head == 0)
      {
      common->recursive_head = common->ovector_start;
      common->ovector_start += sizeof(sljit_sw);
      }
    cc += 1 + LINK_SIZE;
    break;

    case OP_MARK:
    if (common->mark_ptr == 0)
      {
      common->mark_ptr = common->ovector_start;
      common->ovector_start += sizeof(sljit_sw);
      }
    cc += 1 + 2 + cc[1];
    break;

    default:
    cc = next_opcode(common, cc);
    if (cc == NULL)
      return -1;
    break;
    }

  if (space > 0 && cc >= end)
    private_data_length += sizeof(sljit_sw) * space;

  if (size != 0)
    {
    if (size < 0)
      {
      cc += -size;
#ifdef SUPPORT_UTF
      if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
      }
    else
      cc += size;
    }

  if (bracketlen != 0)
    {
    if (cc >= end)
      {
      end = bracketend(cc);
      if (end[-1 - LINK_SIZE] == OP_KET)
        end = NULL;
      }
    cc += bracketlen;
    }
  }
return private_data_length;
}

static void set_private_data_ptrs(compiler_common *common, int private_data_ptr, pcre_uchar *ccend)
{
pcre_uchar *cc = common->start;
pcre_uchar *alternative;
pcre_uchar *end = NULL;
int space, size, bracketlen;

while (cc < ccend)
  {
  space = 0;
  size = 0;
  bracketlen = 0;
  switch(*cc)
    {
    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    case OP_ONCE:
    case OP_ONCE_NC:
    case OP_BRAPOS:
    case OP_SBRA:
    case OP_SBRAPOS:
    case OP_SCOND:
    common->private_data_ptrs[cc - common->start] = private_data_ptr;
    private_data_ptr += sizeof(sljit_sw);
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_CBRAPOS:
    case OP_SCBRAPOS:
    common->private_data_ptrs[cc - common->start] = private_data_ptr;
    private_data_ptr += sizeof(sljit_sw);
    bracketlen = 1 + LINK_SIZE + IMM2_SIZE;
    break;

    case OP_COND:
    /* Might be a hidden SCOND. */
    alternative = cc + GET(cc, 1);
    if (*alternative == OP_KETRMAX || *alternative == OP_KETRMIN)
      {
      common->private_data_ptrs[cc - common->start] = private_data_ptr;
      private_data_ptr += sizeof(sljit_sw);
      }
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_BRA:
    bracketlen = 1 + LINK_SIZE;
    break;

    case OP_CBRA:
    case OP_SCBRA:
    bracketlen = 1 + LINK_SIZE + IMM2_SIZE;
    break;

    CASE_ITERATOR_PRIVATE_DATA_1
    space = 1;
    size = -2;
    break;

    CASE_ITERATOR_PRIVATE_DATA_2A
    space = 2;
    size = -2;
    break;

    CASE_ITERATOR_PRIVATE_DATA_2B
    space = 2;
    size = -(2 + IMM2_SIZE);
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_1
    space = 1;
    size = 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2A
    if (cc[1] != OP_ANYNL && cc[1] != OP_EXTUNI)
      space = 2;
    size = 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2B
    if (cc[1 + IMM2_SIZE] != OP_ANYNL && cc[1 + IMM2_SIZE] != OP_EXTUNI)
      space = 2;
    size = 1 + IMM2_SIZE;
    break;

    case OP_CLASS:
    case OP_NCLASS:
    size += 1 + 32 / sizeof(pcre_uchar);
    space = get_class_iterator_size(cc + size);
    break;

#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
    case OP_XCLASS:
    size = GET(cc, 1);
    space = get_class_iterator_size(cc + size);
    break;
#endif

    default:
    cc = next_opcode(common, cc);
    SLJIT_ASSERT(cc != NULL);
    break;
    }

  if (space > 0 && cc >= end)
    {
    common->private_data_ptrs[cc - common->start] = private_data_ptr;
    private_data_ptr += sizeof(sljit_sw) * space;
    }

  if (size != 0)
    {
    if (size < 0)
      {
      cc += -size;
#ifdef SUPPORT_UTF
      if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
      }
    else
      cc += size;
    }

  if (bracketlen > 0)
    {
    if (cc >= end)
      {
      end = bracketend(cc);
      if (end[-1 - LINK_SIZE] == OP_KET)
        end = NULL;
      }
    cc += bracketlen;
    }
  }
}

/* Returns with -1 if no need for frame. */
static int get_framesize(compiler_common *common, pcre_uchar *cc, BOOL recursive)
{
pcre_uchar *ccend = bracketend(cc);
int length = 0;
BOOL possessive = FALSE;
BOOL setsom_found = recursive;
BOOL setmark_found = recursive;

if (!recursive && (*cc == OP_CBRAPOS || *cc == OP_SCBRAPOS))
  {
  length = 3;
  possessive = TRUE;
  }

cc = next_opcode(common, cc);
SLJIT_ASSERT(cc != NULL);
while (cc < ccend)
  switch(*cc)
    {
    case OP_SET_SOM:
    SLJIT_ASSERT(common->has_set_som);
    if (!setsom_found)
      {
      length += 2;
      setsom_found = TRUE;
      }
    cc += 1;
    break;

    case OP_MARK:
    SLJIT_ASSERT(common->mark_ptr != 0);
    if (!setmark_found)
      {
      length += 2;
      setmark_found = TRUE;
      }
    cc += 1 + 2 + cc[1];
    break;

    case OP_RECURSE:
    if (common->has_set_som && !setsom_found)
      {
      length += 2;
      setsom_found = TRUE;
      }
    if (common->mark_ptr != 0 && !setmark_found)
      {
      length += 2;
      setmark_found = TRUE;
      }
    cc += 1 + LINK_SIZE;
    break;

    case OP_CBRA:
    case OP_CBRAPOS:
    case OP_SCBRA:
    case OP_SCBRAPOS:
    length += 3;
    cc += 1 + LINK_SIZE + IMM2_SIZE;
    break;

    default:
    cc = next_opcode(common, cc);
    SLJIT_ASSERT(cc != NULL);
    break;
    }

/* Possessive quantifiers can use a special case. */
if (SLJIT_UNLIKELY(possessive) && length == 3)
  return -1;

if (length > 0)
  return length + 1;
return -1;
}

static void init_frame(compiler_common *common, pcre_uchar *cc, int stackpos, int stacktop, BOOL recursive)
{
DEFINE_COMPILER;
pcre_uchar *ccend = bracketend(cc);
BOOL setsom_found = recursive;
BOOL setmark_found = recursive;
int offset;

/* >= 1 + shortest item size (2) */
SLJIT_UNUSED_ARG(stacktop);
SLJIT_ASSERT(stackpos >= stacktop + 2);

stackpos = STACK(stackpos);
if (recursive || (*cc != OP_CBRAPOS && *cc != OP_SCBRAPOS))
  cc = next_opcode(common, cc);
SLJIT_ASSERT(cc != NULL);
while (cc < ccend)
  switch(*cc)
    {
    case OP_SET_SOM:
    SLJIT_ASSERT(common->has_set_som);
    if (!setsom_found)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, frame_setstrbegin);
      stackpos += (int)sizeof(sljit_sw);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP1, 0);
      stackpos += (int)sizeof(sljit_sw);
      setsom_found = TRUE;
      }
    cc += 1;
    break;

    case OP_MARK:
    SLJIT_ASSERT(common->mark_ptr != 0);
    if (!setmark_found)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, frame_setmark);
      stackpos += (int)sizeof(sljit_sw);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP1, 0);
      stackpos += (int)sizeof(sljit_sw);
      setmark_found = TRUE;
      }
    cc += 1 + 2 + cc[1];
    break;

    case OP_RECURSE:
    if (common->has_set_som && !setsom_found)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, frame_setstrbegin);
      stackpos += (int)sizeof(sljit_sw);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP1, 0);
      stackpos += (int)sizeof(sljit_sw);
      setsom_found = TRUE;
      }
    if (common->mark_ptr != 0 && !setmark_found)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, frame_setmark);
      stackpos += (int)sizeof(sljit_sw);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP1, 0);
      stackpos += (int)sizeof(sljit_sw);
      setmark_found = TRUE;
      }
    cc += 1 + LINK_SIZE;
    break;

    case OP_CBRA:
    case OP_CBRAPOS:
    case OP_SCBRA:
    case OP_SCBRAPOS:
    offset = (GET2(cc, 1 + LINK_SIZE)) << 1;
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, OVECTOR(offset));
    stackpos += (int)sizeof(sljit_sw);
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP1, 0);
    stackpos += (int)sizeof(sljit_sw);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, TMP2, 0);
    stackpos += (int)sizeof(sljit_sw);

    cc += 1 + LINK_SIZE + IMM2_SIZE;
    break;

    default:
    cc = next_opcode(common, cc);
    SLJIT_ASSERT(cc != NULL);
    break;
    }

OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackpos, SLJIT_IMM, frame_end);
SLJIT_ASSERT(stackpos == STACK(stacktop));
}

static SLJIT_INLINE int get_private_data_length_for_copy(compiler_common *common, pcre_uchar *cc, pcre_uchar *ccend)
{
int private_data_length = 2;
int size;
pcre_uchar *alternative;
/* Calculate the sum of the private machine words. */
while (cc < ccend)
  {
  size = 0;
  switch(*cc)
    {
    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    case OP_ONCE:
    case OP_ONCE_NC:
    case OP_BRAPOS:
    case OP_SBRA:
    case OP_SBRAPOS:
    case OP_SCOND:
    private_data_length++;
    cc += 1 + LINK_SIZE;
    break;

    case OP_CBRA:
    case OP_SCBRA:
    if (common->optimized_cbracket[GET2(cc, 1 + LINK_SIZE)] == 0)
      private_data_length++;
    cc += 1 + LINK_SIZE + IMM2_SIZE;
    break;

    case OP_CBRAPOS:
    case OP_SCBRAPOS:
    private_data_length += 2;
    cc += 1 + LINK_SIZE + IMM2_SIZE;
    break;

    case OP_COND:
    /* Might be a hidden SCOND. */
    alternative = cc + GET(cc, 1);
    if (*alternative == OP_KETRMAX || *alternative == OP_KETRMIN)
      private_data_length++;
    cc += 1 + LINK_SIZE;
    break;

    CASE_ITERATOR_PRIVATE_DATA_1
    if (PRIVATE_DATA(cc))
      private_data_length++;
    cc += 2;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
    break;

    CASE_ITERATOR_PRIVATE_DATA_2A
    if (PRIVATE_DATA(cc))
      private_data_length += 2;
    cc += 2;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
    break;

    CASE_ITERATOR_PRIVATE_DATA_2B
    if (PRIVATE_DATA(cc))
      private_data_length += 2;
    cc += 2 + IMM2_SIZE;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_1
    if (PRIVATE_DATA(cc))
      private_data_length++;
    cc += 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2A
    if (PRIVATE_DATA(cc))
      private_data_length += 2;
    cc += 1;
    break;

    CASE_ITERATOR_TYPE_PRIVATE_DATA_2B
    if (PRIVATE_DATA(cc))
      private_data_length += 2;
    cc += 1 + IMM2_SIZE;
    break;

    case OP_CLASS:
    case OP_NCLASS:
#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
    case OP_XCLASS:
    size = (*cc == OP_XCLASS) ? GET(cc, 1) : 1 + 32 / (int)sizeof(pcre_uchar);
#else
    size = 1 + 32 / (int)sizeof(pcre_uchar);
#endif
    if (PRIVATE_DATA(cc))
      private_data_length += get_class_iterator_size(cc + size);
    cc += size;
    break;

    default:
    cc = next_opcode(common, cc);
    SLJIT_ASSERT(cc != NULL);
    break;
    }
  }
SLJIT_ASSERT(cc == ccend);
return private_data_length;
}

static void copy_private_data(compiler_common *common, pcre_uchar *cc, pcre_uchar *ccend,
  BOOL save, int stackptr, int stacktop)
{
DEFINE_COMPILER;
int srcw[2];
int count, size;
BOOL tmp1next = TRUE;
BOOL tmp1empty = TRUE;
BOOL tmp2empty = TRUE;
pcre_uchar *alternative;
enum {
  start,
  loop,
  end
} status;

status = save ? start : loop;
stackptr = STACK(stackptr - 2);
stacktop = STACK(stacktop - 1);

if (!save)
  {
  stackptr += sizeof(sljit_sw);
  if (stackptr < stacktop)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), stackptr);
    stackptr += sizeof(sljit_sw);
    tmp1empty = FALSE;
    }
  if (stackptr < stacktop)
    {
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), stackptr);
    stackptr += sizeof(sljit_sw);
    tmp2empty = FALSE;
    }
  /* The tmp1next must be TRUE in either way. */
  }

while (status != end)
  {
  count = 0;
  switch(status)
    {
    case start:
    SLJIT_ASSERT(save && common->recursive_head != 0);
    count = 1;
    srcw[0] = common->recursive_head;
    status = loop;
    break;

    case loop:
    if (cc >= ccend)
      {
      status = end;
      break;
      }

    switch(*cc)
      {
      case OP_ASSERT:
      case OP_ASSERT_NOT:
      case OP_ASSERTBACK:
      case OP_ASSERTBACK_NOT:
      case OP_ONCE:
      case OP_ONCE_NC:
      case OP_BRAPOS:
      case OP_SBRA:
      case OP_SBRAPOS:
      case OP_SCOND:
      count = 1;
      srcw[0] = PRIVATE_DATA(cc);
      SLJIT_ASSERT(srcw[0] != 0);
      cc += 1 + LINK_SIZE;
      break;

      case OP_CBRA:
      case OP_SCBRA:
      if (common->optimized_cbracket[GET2(cc, 1 + LINK_SIZE)] == 0)
        {
        count = 1;
        srcw[0] = OVECTOR_PRIV(GET2(cc, 1 + LINK_SIZE));
        }
      cc += 1 + LINK_SIZE + IMM2_SIZE;
      break;

      case OP_CBRAPOS:
      case OP_SCBRAPOS:
      count = 2;
      srcw[0] = PRIVATE_DATA(cc);
      srcw[1] = OVECTOR_PRIV(GET2(cc, 1 + LINK_SIZE));
      SLJIT_ASSERT(srcw[0] != 0 && srcw[1] != 0);
      cc += 1 + LINK_SIZE + IMM2_SIZE;
      break;

      case OP_COND:
      /* Might be a hidden SCOND. */
      alternative = cc + GET(cc, 1);
      if (*alternative == OP_KETRMAX || *alternative == OP_KETRMIN)
        {
        count = 1;
        srcw[0] = PRIVATE_DATA(cc);
        SLJIT_ASSERT(srcw[0] != 0);
        }
      cc += 1 + LINK_SIZE;
      break;

      CASE_ITERATOR_PRIVATE_DATA_1
      if (PRIVATE_DATA(cc))
        {
        count = 1;
        srcw[0] = PRIVATE_DATA(cc);
        }
      cc += 2;
#ifdef SUPPORT_UTF
      if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
      break;

      CASE_ITERATOR_PRIVATE_DATA_2A
      if (PRIVATE_DATA(cc))
        {
        count = 2;
        srcw[0] = PRIVATE_DATA(cc);
        srcw[1] = PRIVATE_DATA(cc) + sizeof(sljit_sw);
        }
      cc += 2;
#ifdef SUPPORT_UTF
      if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
      break;

      CASE_ITERATOR_PRIVATE_DATA_2B
      if (PRIVATE_DATA(cc))
        {
        count = 2;
        srcw[0] = PRIVATE_DATA(cc);
        srcw[1] = PRIVATE_DATA(cc) + sizeof(sljit_sw);
        }
      cc += 2 + IMM2_SIZE;
#ifdef SUPPORT_UTF
      if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
      break;

      CASE_ITERATOR_TYPE_PRIVATE_DATA_1
      if (PRIVATE_DATA(cc))
        {
        count = 1;
        srcw[0] = PRIVATE_DATA(cc);
        }
      cc += 1;
      break;

      CASE_ITERATOR_TYPE_PRIVATE_DATA_2A
      if (PRIVATE_DATA(cc))
        {
        count = 2;
        srcw[0] = PRIVATE_DATA(cc);
        srcw[1] = srcw[0] + sizeof(sljit_sw);
        }
      cc += 1;
      break;

      CASE_ITERATOR_TYPE_PRIVATE_DATA_2B
      if (PRIVATE_DATA(cc))
        {
        count = 2;
        srcw[0] = PRIVATE_DATA(cc);
        srcw[1] = srcw[0] + sizeof(sljit_sw);
        }
      cc += 1 + IMM2_SIZE;
      break;

      case OP_CLASS:
      case OP_NCLASS:
#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
      case OP_XCLASS:
      size = (*cc == OP_XCLASS) ? GET(cc, 1) : 1 + 32 / (int)sizeof(pcre_uchar);
#else
      size = 1 + 32 / (int)sizeof(pcre_uchar);
#endif
      if (PRIVATE_DATA(cc))
        switch(get_class_iterator_size(cc + size))
          {
          case 1:
          count = 1;
          srcw[0] = PRIVATE_DATA(cc);
          break;

          case 2:
          count = 2;
          srcw[0] = PRIVATE_DATA(cc);
          srcw[1] = srcw[0] + sizeof(sljit_sw);
          break;

          default:
          SLJIT_ASSERT_STOP();
          break;
          }
      cc += size;
      break;

      default:
      cc = next_opcode(common, cc);
      SLJIT_ASSERT(cc != NULL);
      break;
      }
    break;

    case end:
    SLJIT_ASSERT_STOP();
    break;
    }

  while (count > 0)
    {
    count--;
    if (save)
      {
      if (tmp1next)
        {
        if (!tmp1empty)
          {
          OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP1, 0);
          stackptr += sizeof(sljit_sw);
          }
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), srcw[count]);
        tmp1empty = FALSE;
        tmp1next = FALSE;
        }
      else
        {
        if (!tmp2empty)
          {
          OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP2, 0);
          stackptr += sizeof(sljit_sw);
          }
        OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), srcw[count]);
        tmp2empty = FALSE;
        tmp1next = TRUE;
        }
      }
    else
      {
      if (tmp1next)
        {
        SLJIT_ASSERT(!tmp1empty);
        OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), srcw[count], TMP1, 0);
        tmp1empty = stackptr >= stacktop;
        if (!tmp1empty)
          {
          OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), stackptr);
          stackptr += sizeof(sljit_sw);
          }
        tmp1next = FALSE;
        }
      else
        {
        SLJIT_ASSERT(!tmp2empty);
        OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), srcw[count], TMP2, 0);
        tmp2empty = stackptr >= stacktop;
        if (!tmp2empty)
          {
          OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), stackptr);
          stackptr += sizeof(sljit_sw);
          }
        tmp1next = TRUE;
        }
      }
    }
  }

if (save)
  {
  if (tmp1next)
    {
    if (!tmp1empty)
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP1, 0);
      stackptr += sizeof(sljit_sw);
      }
    if (!tmp2empty)
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP2, 0);
      stackptr += sizeof(sljit_sw);
      }
    }
  else
    {
    if (!tmp2empty)
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP2, 0);
      stackptr += sizeof(sljit_sw);
      }
    if (!tmp1empty)
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), stackptr, TMP1, 0);
      stackptr += sizeof(sljit_sw);
      }
    }
  }
SLJIT_ASSERT(cc == ccend && stackptr == stacktop && (save || (tmp1empty && tmp2empty)));
}

#undef CASE_ITERATOR_PRIVATE_DATA_1
#undef CASE_ITERATOR_PRIVATE_DATA_2A
#undef CASE_ITERATOR_PRIVATE_DATA_2B
#undef CASE_ITERATOR_TYPE_PRIVATE_DATA_1
#undef CASE_ITERATOR_TYPE_PRIVATE_DATA_2A
#undef CASE_ITERATOR_TYPE_PRIVATE_DATA_2B

static SLJIT_INLINE BOOL is_powerof2(unsigned int value)
{
return (value & (value - 1)) == 0;
}

static SLJIT_INLINE void set_jumps(jump_list *list, struct sljit_label *label)
{
while (list)
  {
  /* sljit_set_label is clever enough to do nothing
  if either the jump or the label is NULL. */
  sljit_set_label(list->jump, label);
  list = list->next;
  }
}

static SLJIT_INLINE void add_jump(struct sljit_compiler *compiler, jump_list **list, struct sljit_jump* jump)
{
jump_list *list_item = sljit_alloc_memory(compiler, sizeof(jump_list));
if (list_item)
  {
  list_item->next = *list;
  list_item->jump = jump;
  *list = list_item;
  }
}

static void add_stub(compiler_common *common, enum stub_types type, int data, struct sljit_jump *start)
{
DEFINE_COMPILER;
stub_list* list_item = sljit_alloc_memory(compiler, sizeof(stub_list));

if (list_item)
  {
  list_item->type = type;
  list_item->data = data;
  list_item->start = start;
  list_item->quit = LABEL();
  list_item->next = common->stubs;
  common->stubs = list_item;
  }
}

static void flush_stubs(compiler_common *common)
{
DEFINE_COMPILER;
stub_list* list_item = common->stubs;

while (list_item)
  {
  JUMPHERE(list_item->start);
  switch(list_item->type)
    {
    case stack_alloc:
    add_jump(compiler, &common->stackalloc, JUMP(SLJIT_FAST_CALL));
    break;
    }
  JUMPTO(SLJIT_JUMP, list_item->quit);
  list_item = list_item->next;
  }
common->stubs = NULL;
}

static SLJIT_INLINE void decrease_call_count(compiler_common *common)
{
DEFINE_COMPILER;

OP2(SLJIT_SUB | SLJIT_SET_E, CALL_COUNT, 0, CALL_COUNT, 0, SLJIT_IMM, 1);
add_jump(compiler, &common->calllimit, JUMP(SLJIT_C_ZERO));
}

static SLJIT_INLINE void allocate_stack(compiler_common *common, int size)
{
/* May destroy all locals and registers except TMP2. */
DEFINE_COMPILER;

OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, size * sizeof(sljit_sw));
#ifdef DESTROY_REGISTERS
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 12345);
OP1(SLJIT_MOV, TMP3, 0, TMP1, 0);
OP1(SLJIT_MOV, RETURN_ADDR, 0, TMP1, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, TMP1, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, TMP1, 0);
#endif
add_stub(common, stack_alloc, 0, CMP(SLJIT_C_GREATER, STACK_TOP, 0, STACK_LIMIT, 0));
}

static SLJIT_INLINE void free_stack(compiler_common *common, int size)
{
DEFINE_COMPILER;
OP2(SLJIT_SUB, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, size * sizeof(sljit_sw));
}

static SLJIT_INLINE void reset_ovector(compiler_common *common, int length)
{
DEFINE_COMPILER;
struct sljit_label *loop;
int i;
/* At this point we can freely use all temporary registers. */
/* TMP1 returns with begin - 1. */
OP2(SLJIT_SUB, SLJIT_SCRATCH_REG1, 0, SLJIT_MEM1(SLJIT_SAVED_REG1), SLJIT_OFFSETOF(jit_arguments, begin), SLJIT_IMM, IN_UCHARS(1));
if (length < 8)
  {
  for (i = 0; i < length; i++)
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(i), SLJIT_SCRATCH_REG1, 0);
  }
else
  {
  GET_LOCAL_BASE(SLJIT_SCRATCH_REG2, 0, OVECTOR_START - sizeof(sljit_sw));
  OP1(SLJIT_MOV, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, length);
  loop = LABEL();
  OP1(SLJIT_MOVU, SLJIT_MEM1(SLJIT_SCRATCH_REG2), sizeof(sljit_sw), SLJIT_SCRATCH_REG1, 0);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_SCRATCH_REG3, 0, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, 1);
  JUMPTO(SLJIT_C_NOT_ZERO, loop);
  }
}

static SLJIT_INLINE void copy_ovector(compiler_common *common, int topbracket)
{
DEFINE_COMPILER;
struct sljit_label *loop;
struct sljit_jump *earlyexit;

/* At this point we can freely use all registers. */
OP1(SLJIT_MOV, SLJIT_SAVED_REG3, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1));
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1), STR_PTR, 0);

OP1(SLJIT_MOV, SLJIT_SCRATCH_REG1, 0, ARGUMENTS, 0);
if (common->mark_ptr != 0)
  OP1(SLJIT_MOV, SLJIT_SCRATCH_REG3, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr);
OP1(SLJIT_MOV_SI, SLJIT_SCRATCH_REG2, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG1), SLJIT_OFFSETOF(jit_arguments, offsetcount));
if (common->mark_ptr != 0)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_SCRATCH_REG1), SLJIT_OFFSETOF(jit_arguments, mark_ptr), SLJIT_SCRATCH_REG3, 0);
OP2(SLJIT_SUB, SLJIT_SCRATCH_REG3, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG1), SLJIT_OFFSETOF(jit_arguments, offsets), SLJIT_IMM, sizeof(int));
OP1(SLJIT_MOV, SLJIT_SCRATCH_REG1, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG1), SLJIT_OFFSETOF(jit_arguments, begin));
GET_LOCAL_BASE(SLJIT_SAVED_REG1, 0, OVECTOR_START);
/* Unlikely, but possible */
earlyexit = CMP(SLJIT_C_EQUAL, SLJIT_SCRATCH_REG2, 0, SLJIT_IMM, 0);
loop = LABEL();
OP2(SLJIT_SUB, SLJIT_SAVED_REG2, 0, SLJIT_MEM1(SLJIT_SAVED_REG1), 0, SLJIT_SCRATCH_REG1, 0);
OP2(SLJIT_ADD, SLJIT_SAVED_REG1, 0, SLJIT_SAVED_REG1, 0, SLJIT_IMM, sizeof(sljit_sw));
/* Copy the integer value to the output buffer */
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
OP2(SLJIT_ASHR, SLJIT_SAVED_REG2, 0, SLJIT_SAVED_REG2, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
OP1(SLJIT_MOVU_SI, SLJIT_MEM1(SLJIT_SCRATCH_REG3), sizeof(int), SLJIT_SAVED_REG2, 0);
OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_SCRATCH_REG2, 0, SLJIT_SCRATCH_REG2, 0, SLJIT_IMM, 1);
JUMPTO(SLJIT_C_NOT_ZERO, loop);
JUMPHERE(earlyexit);

/* Calculate the return value, which is the maximum ovector value. */
if (topbracket > 1)
  {
  GET_LOCAL_BASE(SLJIT_SCRATCH_REG1, 0, OVECTOR_START + topbracket * 2 * sizeof(sljit_sw));
  OP1(SLJIT_MOV, SLJIT_SCRATCH_REG2, 0, SLJIT_IMM, topbracket + 1);

  /* OVECTOR(0) is never equal to SLJIT_SAVED_REG3. */
  loop = LABEL();
  OP1(SLJIT_MOVU, SLJIT_SCRATCH_REG3, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG1), -(2 * (sljit_sw)sizeof(sljit_sw)));
  OP2(SLJIT_SUB, SLJIT_SCRATCH_REG2, 0, SLJIT_SCRATCH_REG2, 0, SLJIT_IMM, 1);
  CMPTO(SLJIT_C_EQUAL, SLJIT_SCRATCH_REG3, 0, SLJIT_SAVED_REG3, 0, loop);
  OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_SCRATCH_REG2, 0);
  }
else
  OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, 1);
}

static SLJIT_INLINE void return_with_partial_match(compiler_common *common, struct sljit_label *quit)
{
DEFINE_COMPILER;

SLJIT_COMPILE_ASSERT(STR_END == SLJIT_SAVED_REG2, str_end_must_be_saved_reg2);
SLJIT_ASSERT(common->start_used_ptr != 0 && (common->mode == JIT_PARTIAL_SOFT_COMPILE ? common->hit_start != 0 : common->hit_start == 0));

OP1(SLJIT_MOV, SLJIT_SCRATCH_REG2, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, PCRE_ERROR_PARTIAL);
OP1(SLJIT_MOV_SI, SLJIT_SCRATCH_REG3, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG2), SLJIT_OFFSETOF(jit_arguments, offsetcount));
CMPTO(SLJIT_C_LESS, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, 2, quit);

/* Store match begin and end. */
OP1(SLJIT_MOV, SLJIT_SAVED_REG1, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG2), SLJIT_OFFSETOF(jit_arguments, begin));
OP1(SLJIT_MOV, SLJIT_SCRATCH_REG2, 0, SLJIT_MEM1(SLJIT_SCRATCH_REG2), SLJIT_OFFSETOF(jit_arguments, offsets));
OP1(SLJIT_MOV, SLJIT_SCRATCH_REG3, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mode == JIT_PARTIAL_HARD_COMPILE ? common->start_used_ptr : common->hit_start);
OP2(SLJIT_SUB, SLJIT_SAVED_REG2, 0, STR_END, 0, SLJIT_SAVED_REG1, 0);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
OP2(SLJIT_ASHR, SLJIT_SAVED_REG2, 0, SLJIT_SAVED_REG2, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
OP1(SLJIT_MOV_SI, SLJIT_MEM1(SLJIT_SCRATCH_REG2), sizeof(int), SLJIT_SAVED_REG2, 0);

OP2(SLJIT_SUB, SLJIT_SCRATCH_REG3, 0, SLJIT_SCRATCH_REG3, 0, SLJIT_SAVED_REG1, 0);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
OP2(SLJIT_ASHR, SLJIT_SCRATCH_REG3, 0, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
OP1(SLJIT_MOV_SI, SLJIT_MEM1(SLJIT_SCRATCH_REG2), 0, SLJIT_SCRATCH_REG3, 0);

JUMPTO(SLJIT_JUMP, quit);
}

static SLJIT_INLINE void check_start_used_ptr(compiler_common *common)
{
/* May destroy TMP1. */
DEFINE_COMPILER;
struct sljit_jump *jump;

if (common->mode == JIT_PARTIAL_SOFT_COMPILE)
  {
  /* The value of -1 must be kept for start_used_ptr! */
  OP2(SLJIT_ADD, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, SLJIT_IMM, 1);
  /* Jumps if start_used_ptr < STR_PTR, or start_used_ptr == -1. Although overwriting
  is not necessary if start_used_ptr == STR_PTR, it does not hurt as well. */
  jump = CMP(SLJIT_C_LESS_EQUAL, TMP1, 0, STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  JUMPHERE(jump);
  }
else if (common->mode == JIT_PARTIAL_HARD_COMPILE)
  {
  jump = CMP(SLJIT_C_LESS_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  JUMPHERE(jump);
  }
}

static SLJIT_INLINE BOOL char_has_othercase(compiler_common *common, pcre_uchar* cc)
{
/* Detects if the character has an othercase. */
unsigned int c;

#ifdef SUPPORT_UTF
if (common->utf)
  {
  GETCHAR(c, cc);
  if (c > 127)
    {
#ifdef SUPPORT_UCP
    return c != UCD_OTHERCASE(c);
#else
    return FALSE;
#endif
    }
#ifndef COMPILE_PCRE8
  return common->fcc[c] != c;
#endif
  }
else
#endif
  c = *cc;
return MAX_255(c) ? common->fcc[c] != c : FALSE;
}

static SLJIT_INLINE unsigned int char_othercase(compiler_common *common, unsigned int c)
{
/* Returns with the othercase. */
#ifdef SUPPORT_UTF
if (common->utf && c > 127)
  {
#ifdef SUPPORT_UCP
  return UCD_OTHERCASE(c);
#else
  return c;
#endif
  }
#endif
return TABLE_GET(c, common->fcc, c);
}

static unsigned int char_get_othercase_bit(compiler_common *common, pcre_uchar* cc)
{
/* Detects if the character and its othercase has only 1 bit difference. */
unsigned int c, oc, bit;
#if defined SUPPORT_UTF && defined COMPILE_PCRE8
int n;
#endif

#ifdef SUPPORT_UTF
if (common->utf)
  {
  GETCHAR(c, cc);
  if (c <= 127)
    oc = common->fcc[c];
  else
    {
#ifdef SUPPORT_UCP
    oc = UCD_OTHERCASE(c);
#else
    oc = c;
#endif
    }
  }
else
  {
  c = *cc;
  oc = TABLE_GET(c, common->fcc, c);
  }
#else
c = *cc;
oc = TABLE_GET(c, common->fcc, c);
#endif

SLJIT_ASSERT(c != oc);

bit = c ^ oc;
/* Optimized for English alphabet. */
if (c <= 127 && bit == 0x20)
  return (0 << 8) | 0x20;

/* Since c != oc, they must have at least 1 bit difference. */
if (!is_powerof2(bit))
  return 0;

#if defined COMPILE_PCRE8

#ifdef SUPPORT_UTF
if (common->utf && c > 127)
  {
  n = GET_EXTRALEN(*cc);
  while ((bit & 0x3f) == 0)
    {
    n--;
    bit >>= 6;
    }
  return (n << 8) | bit;
  }
#endif /* SUPPORT_UTF */
return (0 << 8) | bit;

#elif defined COMPILE_PCRE16 || defined COMPILE_PCRE32

#ifdef SUPPORT_UTF
if (common->utf && c > 65535)
  {
  if (bit >= (1 << 10))
    bit >>= 10;
  else
    return (bit < 256) ? ((2 << 8) | bit) : ((3 << 8) | (bit >> 8));
  }
#endif /* SUPPORT_UTF */
return (bit < 256) ? ((0 << 8) | bit) : ((1 << 8) | (bit >> 8));

#endif /* COMPILE_PCRE[8|16|32] */
}

static void check_partial(compiler_common *common, BOOL force)
{
/* Checks whether a partial matching is occured. Does not modify registers. */
DEFINE_COMPILER;
struct sljit_jump *jump = NULL;

SLJIT_ASSERT(!force || common->mode != JIT_COMPILE);

if (common->mode == JIT_COMPILE)
  return;

if (!force)
  jump = CMP(SLJIT_C_GREATER_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
else if (common->mode == JIT_PARTIAL_SOFT_COMPILE)
  jump = CMP(SLJIT_C_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, SLJIT_IMM, -1);

if (common->mode == JIT_PARTIAL_SOFT_COMPILE)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, -1);
else
  {
  if (common->partialmatchlabel != NULL)
    JUMPTO(SLJIT_JUMP, common->partialmatchlabel);
  else
    add_jump(compiler, &common->partialmatch, JUMP(SLJIT_JUMP));
  }

if (jump != NULL)
  JUMPHERE(jump);
}

static struct sljit_jump *check_str_end(compiler_common *common)
{
/* Does not affect registers. Usually used in a tight spot. */
DEFINE_COMPILER;
struct sljit_jump *jump;
struct sljit_jump *nohit;
struct sljit_jump *return_value;

if (common->mode == JIT_COMPILE)
  return CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);

jump = CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0);
if (common->mode == JIT_PARTIAL_SOFT_COMPILE)
  {
  nohit = CMP(SLJIT_C_GREATER_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, -1);
  JUMPHERE(nohit);
  return_value = JUMP(SLJIT_JUMP);
  }
else
  {
  return_value = CMP(SLJIT_C_GREATER_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  if (common->partialmatchlabel != NULL)
    JUMPTO(SLJIT_JUMP, common->partialmatchlabel);
  else
    add_jump(compiler, &common->partialmatch, JUMP(SLJIT_JUMP));
  }
JUMPHERE(jump);
return return_value;
}

static void detect_partial_match(compiler_common *common, jump_list **backtracks)
{
DEFINE_COMPILER;
struct sljit_jump *jump;

if (common->mode == JIT_COMPILE)
  {
  add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0));
  return;
  }

/* Partial matching mode. */
jump = CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0);
add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0));
if (common->mode == JIT_PARTIAL_SOFT_COMPILE)
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, -1);
  add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));
  }
else
  {
  if (common->partialmatchlabel != NULL)
    JUMPTO(SLJIT_JUMP, common->partialmatchlabel);
  else
    add_jump(compiler, &common->partialmatch, JUMP(SLJIT_JUMP));
  }
JUMPHERE(jump);
}

static void read_char(compiler_common *common)
{
/* Reads the character into TMP1, updates STR_PTR.
Does not check STR_END. TMP2 Destroyed. */
DEFINE_COMPILER;
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
struct sljit_jump *jump;
#endif

OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
if (common->utf)
  {
#if defined COMPILE_PCRE8
  jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0);
#elif defined COMPILE_PCRE16
  jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xd800);
#endif /* COMPILE_PCRE[8|16] */
  add_jump(compiler, &common->utfreadchar, JUMP(SLJIT_FAST_CALL));
  JUMPHERE(jump);
  }
#endif /* SUPPORT_UTF && !COMPILE_PCRE32 */
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
}

static void peek_char(compiler_common *common)
{
/* Reads the character into TMP1, keeps STR_PTR.
Does not check STR_END. TMP2 Destroyed. */
DEFINE_COMPILER;
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
struct sljit_jump *jump;
#endif

OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
if (common->utf)
  {
#if defined COMPILE_PCRE8
  jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0);
#elif defined COMPILE_PCRE16
  jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xd800);
#endif /* COMPILE_PCRE[8|16] */
  add_jump(compiler, &common->utfreadchar, JUMP(SLJIT_FAST_CALL));
  OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, TMP2, 0);
  JUMPHERE(jump);
  }
#endif /* SUPPORT_UTF && !COMPILE_PCRE32 */
}

static void read_char8_type(compiler_common *common)
{
/* Reads the character type into TMP1, updates STR_PTR. Does not check STR_END. */
DEFINE_COMPILER;
#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
struct sljit_jump *jump;
#endif

#ifdef SUPPORT_UTF
if (common->utf)
  {
  OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), 0);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
#if defined COMPILE_PCRE8
  /* This can be an extra read in some situations, but hopefully
  it is needed in most cases. */
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), common->ctypes);
  jump = CMP(SLJIT_C_LESS, TMP2, 0, SLJIT_IMM, 0xc0);
  add_jump(compiler, &common->utfreadtype8, JUMP(SLJIT_FAST_CALL));
  JUMPHERE(jump);
#elif defined COMPILE_PCRE16
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 0);
  jump = CMP(SLJIT_C_GREATER, TMP2, 0, SLJIT_IMM, 255);
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), common->ctypes);
  JUMPHERE(jump);
  /* Skip low surrogate if necessary. */
  OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0xfc00);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP2, 0, SLJIT_IMM, 0xd800);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 1);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP2, 0);
#elif defined COMPILE_PCRE32
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 0);
  jump = CMP(SLJIT_C_GREATER, TMP2, 0, SLJIT_IMM, 255);
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), common->ctypes);
  JUMPHERE(jump);
#endif /* COMPILE_PCRE[8|16|32] */
  return;
  }
#endif /* SUPPORT_UTF */
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), 0);
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
/* The ctypes array contains only 256 values. */
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 0);
jump = CMP(SLJIT_C_GREATER, TMP2, 0, SLJIT_IMM, 255);
#endif
OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), common->ctypes);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
JUMPHERE(jump);
#endif
}

static void skip_char_back(compiler_common *common)
{
/* Goes one character back. Affects STR_PTR and TMP1. Does not check begin. */
DEFINE_COMPILER;
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
#if defined COMPILE_PCRE8
struct sljit_label *label;

if (common->utf)
  {
  label = LABEL();
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), -IN_UCHARS(1));
  OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0xc0);
  CMPTO(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, 0x80, label);
  return;
  }
#elif defined COMPILE_PCRE16
if (common->utf)
  {
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), -IN_UCHARS(1));
  OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  /* Skip low surrogate if necessary. */
  OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0xfc00);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0xdc00);
  OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  return;
  }
#endif /* COMPILE_PCRE[8|16] */
#endif /* SUPPORT_UTF && !COMPILE_PCRE32 */
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
}

static void check_newlinechar(compiler_common *common, int nltype, jump_list **backtracks, BOOL jumpiftrue)
{
/* Character comes in TMP1. Checks if it is a newline. TMP2 may be destroyed. */
DEFINE_COMPILER;

if (nltype == NLTYPE_ANY)
  {
  add_jump(compiler, &common->anynewline, JUMP(SLJIT_FAST_CALL));
  add_jump(compiler, backtracks, JUMP(jumpiftrue ? SLJIT_C_NOT_ZERO : SLJIT_C_ZERO));
  }
else if (nltype == NLTYPE_ANYCRLF)
  {
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, CHAR_CR);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, CHAR_NL);
  OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  add_jump(compiler, backtracks, JUMP(jumpiftrue ? SLJIT_C_NOT_ZERO : SLJIT_C_ZERO));
  }
else
  {
  SLJIT_ASSERT(nltype == NLTYPE_FIXED && common->newline < 256);
  add_jump(compiler, backtracks, CMP(jumpiftrue ? SLJIT_C_EQUAL : SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, common->newline));
  }
}

#ifdef SUPPORT_UTF

#if defined COMPILE_PCRE8
static void do_utfreadchar(compiler_common *common)
{
/* Fast decoding a UTF-8 character. TMP1 contains the first byte
of the character (>= 0xc0). Return char value in TMP1, length - 1 in TMP2. */
DEFINE_COMPILER;
struct sljit_jump *jump;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
/* Searching for the first zero. */
OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x20);
jump = JUMP(SLJIT_C_NOT_ZERO);
/* Two byte sequence. */
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x1f);
OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 6);
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, IN_UCHARS(1));
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
JUMPHERE(jump);

OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x10);
jump = JUMP(SLJIT_C_NOT_ZERO);
/* Three byte sequence. */
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x0f);
OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 12);
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 6);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(2));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(2));
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, IN_UCHARS(2));
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
JUMPHERE(jump);

/* Four byte sequence. */
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x07);
OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 18);
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 12);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(2));
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 6);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(3));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(3));
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, IN_UCHARS(3));
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

static void do_utfreadtype8(compiler_common *common)
{
/* Fast decoding a UTF-8 character type. TMP2 contains the first byte
of the character (>= 0xc0). Return value in TMP1. */
DEFINE_COMPILER;
struct sljit_jump *jump;
struct sljit_jump *compare;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);

OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP2, 0, SLJIT_IMM, 0x20);
jump = JUMP(SLJIT_C_NOT_ZERO);
/* Two byte sequence. */
OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x1f);
OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 6);
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x3f);
OP2(SLJIT_OR, TMP2, 0, TMP2, 0, TMP1, 0);
compare = CMP(SLJIT_C_GREATER, TMP2, 0, SLJIT_IMM, 255);
OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), common->ctypes);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);

JUMPHERE(compare);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 0);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
JUMPHERE(jump);

/* We only have types for characters less than 256. */
OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP2), (sljit_sw)PRIV(utf8_table4) - 0xc0);
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 0);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

#elif defined COMPILE_PCRE16

static void do_utfreadchar(compiler_common *common)
{
/* Fast decoding a UTF-16 character. TMP1 contains the first 16 bit char
of the character (>= 0xd800). Return char value in TMP1, length - 1 in TMP2. */
DEFINE_COMPILER;
struct sljit_jump *jump;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xdc00);
/* Do nothing, only return. */
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);

JUMPHERE(jump);
/* Combine two 16 bit characters. */
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x3ff);
OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 10);
OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 0x3ff);
OP2(SLJIT_OR, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x10000);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

#endif /* COMPILE_PCRE[8|16] */

#endif /* SUPPORT_UTF */

#ifdef SUPPORT_UCP

/* UCD_BLOCK_SIZE must be 128 (see the assert below). */
#define UCD_BLOCK_MASK 127
#define UCD_BLOCK_SHIFT 7

static void do_getucd(compiler_common *common)
{
/* Search the UCD record for the character comes in TMP1.
Returns chartype in TMP1 and UCD offset in TMP2. */
DEFINE_COMPILER;

SLJIT_ASSERT(UCD_BLOCK_SIZE == 128 && sizeof(ucd_record) == 8);

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
OP2(SLJIT_LSHR, TMP2, 0, TMP1, 0, SLJIT_IMM, UCD_BLOCK_SHIFT);
OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP2), (sljit_sw)PRIV(ucd_stage1));
OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, UCD_BLOCK_MASK);
OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, UCD_BLOCK_SHIFT);
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_stage2));
OP1(SLJIT_MOV_UH, TMP2, 0, SLJIT_MEM2(TMP2, TMP1), 1);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_records) + SLJIT_OFFSETOF(ucd_record, chartype));
OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM2(TMP1, TMP2), 3);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}
#endif

static SLJIT_INLINE struct sljit_label *mainloop_entry(compiler_common *common, BOOL hascrorlf, BOOL firstline)
{
DEFINE_COMPILER;
struct sljit_label *mainloop;
struct sljit_label *newlinelabel = NULL;
struct sljit_jump *start;
struct sljit_jump *end = NULL;
struct sljit_jump *nl = NULL;
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
struct sljit_jump *singlechar;
#endif
jump_list *newline = NULL;
BOOL newlinecheck = FALSE;
BOOL readuchar = FALSE;

if (!(hascrorlf || firstline) && (common->nltype == NLTYPE_ANY ||
    common->nltype == NLTYPE_ANYCRLF || common->newline > 255))
  newlinecheck = TRUE;

if (firstline)
  {
  /* Search for the end of the first line. */
  SLJIT_ASSERT(common->first_line_end != 0);
  OP1(SLJIT_MOV, TMP3, 0, STR_PTR, 0);

  if (common->nltype == NLTYPE_FIXED && common->newline > 255)
    {
    mainloop = LABEL();
    OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
    end = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(-1));
    OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
    CMPTO(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff, mainloop);
    CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, common->newline & 0xff, mainloop);
    JUMPHERE(end);
    OP2(SLJIT_SUB, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
    }
  else
    {
    end = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
    mainloop = LABEL();
    /* Continual stores does not cause data dependency. */
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end, STR_PTR, 0);
    read_char(common);
    check_newlinechar(common, common->nltype, &newline, TRUE);
    CMPTO(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0, mainloop);
    JUMPHERE(end);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end, STR_PTR, 0);
    set_jumps(newline, LABEL());
    }

  OP1(SLJIT_MOV, STR_PTR, 0, TMP3, 0);
  }

start = JUMP(SLJIT_JUMP);

if (newlinecheck)
  {
  newlinelabel = LABEL();
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  end = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, common->newline & 0xff);
  OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
  OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  nl = JUMP(SLJIT_JUMP);
  }

mainloop = LABEL();

/* Increasing the STR_PTR here requires one less jump in the most common case. */
#ifdef SUPPORT_UTF
if (common->utf) readuchar = TRUE;
#endif
if (newlinecheck) readuchar = TRUE;

if (readuchar)
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);

if (newlinecheck)
  CMPTO(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff, newlinelabel);

OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
#if defined SUPPORT_UTF && !defined COMPILE_PCRE32
#if defined COMPILE_PCRE8
if (common->utf)
  {
  singlechar = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0);
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)PRIV(utf8_table4) - 0xc0);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  JUMPHERE(singlechar);
  }
#elif defined COMPILE_PCRE16
if (common->utf)
  {
  singlechar = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xd800);
  OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0xfc00);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0xd800);
  OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  JUMPHERE(singlechar);
  }
#endif /* COMPILE_PCRE[8|16] */
#endif /* SUPPORT_UTF && !COMPILE_PCRE32 */
JUMPHERE(start);

if (newlinecheck)
  {
  JUMPHERE(end);
  JUMPHERE(nl);
  }

return mainloop;
}

#define MAX_N_CHARS 3

static SLJIT_INLINE BOOL fast_forward_first_n_chars(compiler_common *common, BOOL firstline)
{
DEFINE_COMPILER;
struct sljit_label *start;
struct sljit_jump *quit;
pcre_uint32 chars[MAX_N_CHARS * 2];
pcre_uchar *cc = common->start + 1 + IMM2_SIZE;
int location = 0;
pcre_int32 len, c, bit, caseless;
int must_stop;

/* We do not support alternatives now. */
if (*(common->start + GET(common->start, 1)) == OP_ALT)
  return FALSE;

while (TRUE)
  {
  caseless = 0;
  must_stop = 1;
  switch(*cc)
    {
    case OP_CHAR:
    must_stop = 0;
    cc++;
    break;

    case OP_CHARI:
    caseless = 1;
    must_stop = 0;
    cc++;
    break;

    case OP_SOD:
    case OP_SOM:
    case OP_SET_SOM:
    case OP_NOT_WORD_BOUNDARY:
    case OP_WORD_BOUNDARY:
    case OP_EODN:
    case OP_EOD:
    case OP_CIRC:
    case OP_CIRCM:
    case OP_DOLL:
    case OP_DOLLM:
    /* Zero width assertions. */
    cc++;
    continue;

    case OP_PLUS:
    case OP_MINPLUS:
    case OP_POSPLUS:
    cc++;
    break;

    case OP_EXACT:
    cc += 1 + IMM2_SIZE;
    break;

    case OP_PLUSI:
    case OP_MINPLUSI:
    case OP_POSPLUSI:
    caseless = 1;
    cc++;
    break;

    case OP_EXACTI:
    caseless = 1;
    cc += 1 + IMM2_SIZE;
    break;

    default:
    must_stop = 2;
    break;
    }

  if (must_stop == 2)
      break;

  len = 1;
#ifdef SUPPORT_UTF
  if (common->utf && HAS_EXTRALEN(cc[0])) len += GET_EXTRALEN(cc[0]);
#endif

  if (caseless && char_has_othercase(common, cc))
    {
    caseless = char_get_othercase_bit(common, cc);
    if (caseless == 0)
      return FALSE;
#ifdef COMPILE_PCRE8
    caseless = ((caseless & 0xff) << 8) | (len - (caseless >> 8));
#else
    if ((caseless & 0x100) != 0)
      caseless = ((caseless & 0xff) << 16) | (len - (caseless >> 9));
    else
      caseless = ((caseless & 0xff) << 8) | (len - (caseless >> 9));
#endif
    }
  else
    caseless = 0;

  while (len > 0 && location < MAX_N_CHARS * 2)
    {
    c = *cc;
    bit = 0;
    if (len == (caseless & 0xff))
      {
      bit = caseless >> 8;
      c |= bit;
      }

    chars[location] = c;
    chars[location + 1] = bit;

    len--;
    location += 2;
    cc++;
    }

  if (location >= MAX_N_CHARS * 2 || must_stop != 0)
    break;
  }

/* At least two characters are required. */
if (location < 2 * 2)
    return FALSE;

if (firstline)
  {
  SLJIT_ASSERT(common->first_line_end != 0);
  OP1(SLJIT_MOV, TMP3, 0, STR_END, 0);
  OP2(SLJIT_SUB, STR_END, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end, SLJIT_IMM, (location >> 1) - 1);
  }
else
  OP2(SLJIT_SUB, STR_END, 0, STR_END, 0, SLJIT_IMM, (location >> 1) - 1);

start = LABEL();
quit = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);

OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
if (chars[1] != 0)
  OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, chars[1]);
CMPTO(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, chars[0], start);
if (location > 2 * 2)
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
if (chars[3] != 0)
  OP2(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_IMM, chars[3]);
CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, chars[2], start);
if (location > 2 * 2)
  {
  if (chars[5] != 0)
    OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, chars[5]);
  CMPTO(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, chars[4], start);
  }
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));

JUMPHERE(quit);

if (firstline)
  OP1(SLJIT_MOV, STR_END, 0, TMP3, 0);
else
  OP2(SLJIT_ADD, STR_END, 0, STR_END, 0, SLJIT_IMM, (location >> 1) - 1);
return TRUE;
}

#undef MAX_N_CHARS

static SLJIT_INLINE void fast_forward_first_char(compiler_common *common, pcre_uchar first_char, BOOL caseless, BOOL firstline)
{
DEFINE_COMPILER;
struct sljit_label *start;
struct sljit_jump *quit;
struct sljit_jump *found;
pcre_uchar oc, bit;

if (firstline)
  {
  SLJIT_ASSERT(common->first_line_end != 0);
  OP1(SLJIT_MOV, TMP3, 0, STR_END, 0);
  OP1(SLJIT_MOV, STR_END, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end);
  }

start = LABEL();
quit = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);

oc = first_char;
if (caseless)
  {
  oc = TABLE_GET(first_char, common->fcc, first_char);
#if defined SUPPORT_UCP && !(defined COMPILE_PCRE8)
  if (first_char > 127 && common->utf)
    oc = UCD_OTHERCASE(first_char);
#endif
  }
if (first_char == oc)
  found = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, first_char);
else
  {
  bit = first_char ^ oc;
  if (is_powerof2(bit))
    {
    OP2(SLJIT_OR, TMP2, 0, TMP1, 0, SLJIT_IMM, bit);
    found = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, first_char | bit);
    }
  else
    {
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, first_char);
    OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, oc);
    OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
    found = JUMP(SLJIT_C_NOT_ZERO);
    }
  }

OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
JUMPTO(SLJIT_JUMP, start);
JUMPHERE(found);
JUMPHERE(quit);

if (firstline)
  OP1(SLJIT_MOV, STR_END, 0, TMP3, 0);
}

static SLJIT_INLINE void fast_forward_newline(compiler_common *common, BOOL firstline)
{
DEFINE_COMPILER;
struct sljit_label *loop;
struct sljit_jump *lastchar;
struct sljit_jump *firstchar;
struct sljit_jump *quit;
struct sljit_jump *foundcr = NULL;
struct sljit_jump *notfoundnl;
jump_list *newline = NULL;

if (firstline)
  {
  SLJIT_ASSERT(common->first_line_end != 0);
  OP1(SLJIT_MOV, TMP3, 0, STR_END, 0);
  OP1(SLJIT_MOV, STR_END, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end);
  }

if (common->nltype == NLTYPE_FIXED && common->newline > 255)
  {
  lastchar = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, begin));
  firstchar = CMP(SLJIT_C_LESS_EQUAL, STR_PTR, 0, TMP2, 0);

  OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, IN_UCHARS(2));
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, STR_PTR, 0, TMP1, 0);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_GREATER_EQUAL);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
  OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
  OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, TMP2, 0);

  loop = LABEL();
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  quit = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(-2));
  OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(-1));
  CMPTO(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff, loop);
  CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, common->newline & 0xff, loop);

  JUMPHERE(quit);
  JUMPHERE(firstchar);
  JUMPHERE(lastchar);

  if (firstline)
    OP1(SLJIT_MOV, STR_END, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0);
  return;
  }

OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
firstchar = CMP(SLJIT_C_LESS_EQUAL, STR_PTR, 0, TMP2, 0);
skip_char_back(common);

loop = LABEL();
read_char(common);
lastchar = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
if (common->nltype == NLTYPE_ANY || common->nltype == NLTYPE_ANYCRLF)
  foundcr = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_CR);
check_newlinechar(common, common->nltype, &newline, FALSE);
set_jumps(newline, loop);

if (common->nltype == NLTYPE_ANY || common->nltype == NLTYPE_ANYCRLF)
  {
  quit = JUMP(SLJIT_JUMP);
  JUMPHERE(foundcr);
  notfoundnl = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, CHAR_NL);
  OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
#if defined COMPILE_PCRE16 || defined COMPILE_PCRE32
  OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, UCHAR_SHIFT);
#endif
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  JUMPHERE(notfoundnl);
  JUMPHERE(quit);
  }
JUMPHERE(lastchar);
JUMPHERE(firstchar);

if (firstline)
  OP1(SLJIT_MOV, STR_END, 0, TMP3, 0);
}

static SLJIT_INLINE void fast_forward_start_bits(compiler_common *common, sljit_uw start_bits, BOOL firstline)
{
DEFINE_COMPILER;
struct sljit_label *start;
struct sljit_jump *quit;
struct sljit_jump *found;
#ifndef COMPILE_PCRE8
struct sljit_jump *jump;
#endif

if (firstline)
  {
  SLJIT_ASSERT(common->first_line_end != 0);
  OP1(SLJIT_MOV, RETURN_ADDR, 0, STR_END, 0);
  OP1(SLJIT_MOV, STR_END, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end);
  }

start = LABEL();
quit = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
#ifdef SUPPORT_UTF
if (common->utf)
  OP1(SLJIT_MOV, TMP3, 0, TMP1, 0);
#endif
#ifndef COMPILE_PCRE8
jump = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 255);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, 255);
JUMPHERE(jump);
#endif
OP2(SLJIT_AND, TMP2, 0, TMP1, 0, SLJIT_IMM, 0x7);
OP2(SLJIT_LSHR, TMP1, 0, TMP1, 0, SLJIT_IMM, 3);
OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), start_bits);
OP2(SLJIT_SHL, TMP2, 0, SLJIT_IMM, 1, TMP2, 0);
OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, TMP2, 0);
found = JUMP(SLJIT_C_NOT_ZERO);

#ifdef SUPPORT_UTF
if (common->utf)
  OP1(SLJIT_MOV, TMP1, 0, TMP3, 0);
#endif
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
#ifdef SUPPORT_UTF
#if defined COMPILE_PCRE8
if (common->utf)
  {
  CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0, start);
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)PRIV(utf8_table4) - 0xc0);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  }
#elif defined COMPILE_PCRE16
if (common->utf)
  {
  CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xd800, start);
  OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0xfc00);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0xd800);
  OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
  }
#endif /* COMPILE_PCRE[8|16] */
#endif /* SUPPORT_UTF */
JUMPTO(SLJIT_JUMP, start);
JUMPHERE(found);
JUMPHERE(quit);

if (firstline)
  OP1(SLJIT_MOV, STR_END, 0, RETURN_ADDR, 0);
}

static SLJIT_INLINE struct sljit_jump *search_requested_char(compiler_common *common, pcre_uchar req_char, BOOL caseless, BOOL has_firstchar)
{
DEFINE_COMPILER;
struct sljit_label *loop;
struct sljit_jump *toolong;
struct sljit_jump *alreadyfound;
struct sljit_jump *found;
struct sljit_jump *foundoc = NULL;
struct sljit_jump *notfound;
pcre_uint32 oc, bit;

SLJIT_ASSERT(common->req_char_ptr != 0);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->req_char_ptr);
OP2(SLJIT_ADD, TMP1, 0, STR_PTR, 0, SLJIT_IMM, REQ_BYTE_MAX);
toolong = CMP(SLJIT_C_LESS, TMP1, 0, STR_END, 0);
alreadyfound = CMP(SLJIT_C_LESS, STR_PTR, 0, TMP2, 0);

if (has_firstchar)
  OP2(SLJIT_ADD, TMP1, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
else
  OP1(SLJIT_MOV, TMP1, 0, STR_PTR, 0);

loop = LABEL();
notfound = CMP(SLJIT_C_GREATER_EQUAL, TMP1, 0, STR_END, 0);

OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(TMP1), 0);
oc = req_char;
if (caseless)
  {
  oc = TABLE_GET(req_char, common->fcc, req_char);
#if defined SUPPORT_UCP && !(defined COMPILE_PCRE8)
  if (req_char > 127 && common->utf)
    oc = UCD_OTHERCASE(req_char);
#endif
  }
if (req_char == oc)
  found = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, req_char);
else
  {
  bit = req_char ^ oc;
  if (is_powerof2(bit))
    {
    OP2(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_IMM, bit);
    found = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, req_char | bit);
    }
  else
    {
    found = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, req_char);
    foundoc = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, oc);
    }
  }
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, IN_UCHARS(1));
JUMPTO(SLJIT_JUMP, loop);

JUMPHERE(found);
if (foundoc)
  JUMPHERE(foundoc);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->req_char_ptr, TMP1, 0);
JUMPHERE(alreadyfound);
JUMPHERE(toolong);
return notfound;
}

static void do_revertframes(compiler_common *common)
{
DEFINE_COMPILER;
struct sljit_jump *jump;
struct sljit_label *mainloop;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
OP1(SLJIT_MOV, TMP1, 0, STACK_TOP, 0);
GET_LOCAL_BASE(TMP3, 0, 0);

/* Drop frames until we reach STACK_TOP. */
mainloop = LABEL();
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), 0);
jump = CMP(SLJIT_C_SIG_LESS_EQUAL, TMP2, 0, SLJIT_IMM, frame_end);
OP2(SLJIT_ADD, TMP2, 0, TMP2, 0, TMP3, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(TMP2), 0, SLJIT_MEM1(TMP1), sizeof(sljit_sw));
OP1(SLJIT_MOV, SLJIT_MEM1(TMP2), sizeof(sljit_sw), SLJIT_MEM1(TMP1), 2 * sizeof(sljit_sw));
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 3 * sizeof(sljit_sw));
JUMPTO(SLJIT_JUMP, mainloop);

JUMPHERE(jump);
jump = CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, frame_end);
/* End of dropping frames. */
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);

JUMPHERE(jump);
jump = CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, frame_setstrbegin);
/* Set string begin. */
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), sizeof(sljit_sw));
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 2 * sizeof(sljit_sw));
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), TMP2, 0);
JUMPTO(SLJIT_JUMP, mainloop);

JUMPHERE(jump);
if (common->mark_ptr != 0)
  {
  jump = CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, frame_setmark);
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), sizeof(sljit_sw));
  OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 2 * sizeof(sljit_sw));
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr, TMP2, 0);
  JUMPTO(SLJIT_JUMP, mainloop);

  JUMPHERE(jump);
  }

/* Unknown command. */
OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 2 * sizeof(sljit_sw));
JUMPTO(SLJIT_JUMP, mainloop);
}

static void check_wordboundary(compiler_common *common)
{
DEFINE_COMPILER;
struct sljit_jump *skipread;
#if !(defined COMPILE_PCRE8) || defined SUPPORT_UTF
struct sljit_jump *jump;
#endif

SLJIT_COMPILE_ASSERT(ctype_word == 0x10, ctype_word_must_be_16);

sljit_emit_fast_enter(compiler, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
/* Get type of the previous char, and put it to LOCALS1. */
OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, begin));
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, SLJIT_IMM, 0);
skipread = CMP(SLJIT_C_LESS_EQUAL, STR_PTR, 0, TMP1, 0);
skip_char_back(common);
check_start_used_ptr(common);
read_char(common);

/* Testing char type. */
#ifdef SUPPORT_UCP
if (common->use_ucp)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 1);
  jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_UNDERSCORE);
  add_jump(compiler, &common->getucd, JUMP(SLJIT_FAST_CALL));
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ucp_Ll);
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ucp_Lu - ucp_Ll);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS_EQUAL);
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ucp_Nd - ucp_Ll);
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ucp_No - ucp_Nd);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
  JUMPHERE(jump);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, TMP2, 0);
  }
else
#endif
  {
#ifndef COMPILE_PCRE8
  jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#elif defined SUPPORT_UTF
  /* Here LOCALS1 has already been zeroed. */
  jump = NULL;
  if (common->utf)
    jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#endif /* COMPILE_PCRE8 */
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), common->ctypes);
  OP2(SLJIT_LSHR, TMP1, 0, TMP1, 0, SLJIT_IMM, 4 /* ctype_word */);
  OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, TMP1, 0);
#ifndef COMPILE_PCRE8
  JUMPHERE(jump);
#elif defined SUPPORT_UTF
  if (jump != NULL)
    JUMPHERE(jump);
#endif /* COMPILE_PCRE8 */
  }
JUMPHERE(skipread);

OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 0);
skipread = check_str_end(common);
peek_char(common);

/* Testing char type. This is a code duplication. */
#ifdef SUPPORT_UCP
if (common->use_ucp)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 1);
  jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_UNDERSCORE);
  add_jump(compiler, &common->getucd, JUMP(SLJIT_FAST_CALL));
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ucp_Ll);
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ucp_Lu - ucp_Ll);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS_EQUAL);
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ucp_Nd - ucp_Ll);
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ucp_No - ucp_Nd);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
  JUMPHERE(jump);
  }
else
#endif
  {
#ifndef COMPILE_PCRE8
  /* TMP2 may be destroyed by peek_char. */
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 0);
  jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#elif defined SUPPORT_UTF
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 0);
  jump = NULL;
  if (common->utf)
    jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#endif
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP1), common->ctypes);
  OP2(SLJIT_LSHR, TMP2, 0, TMP2, 0, SLJIT_IMM, 4 /* ctype_word */);
  OP2(SLJIT_AND, TMP2, 0, TMP2, 0, SLJIT_IMM, 1);
#ifndef COMPILE_PCRE8
  JUMPHERE(jump);
#elif defined SUPPORT_UTF
  if (jump != NULL)
    JUMPHERE(jump);
#endif /* COMPILE_PCRE8 */
  }
JUMPHERE(skipread);

OP2(SLJIT_XOR | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1);
sljit_emit_fast_return(compiler, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
}

/*
  range format:

  ranges[0] = length of the range (max MAX_RANGE_SIZE, -1 means invalid range).
  ranges[1] = first bit (0 or 1)
  ranges[2-length] = position of the bit change (when the current bit is not equal to the previous)
*/

static BOOL check_ranges(compiler_common *common, int *ranges, jump_list **backtracks, BOOL readch)
{
DEFINE_COMPILER;
struct sljit_jump *jump;

if (ranges[0] < 0)
  return FALSE;

switch(ranges[0])
  {
  case 1:
  if (readch)
    read_char(common);
  add_jump(compiler, backtracks, CMP(ranges[1] == 0 ? SLJIT_C_LESS : SLJIT_C_GREATER_EQUAL, TMP1, 0, SLJIT_IMM, ranges[2]));
  return TRUE;

  case 2:
  if (readch)
    read_char(common);
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ranges[2]);
  add_jump(compiler, backtracks, CMP(ranges[1] != 0 ? SLJIT_C_LESS : SLJIT_C_GREATER_EQUAL, TMP1, 0, SLJIT_IMM, ranges[3] - ranges[2]));
  return TRUE;

  case 4:
  if (ranges[2] + 1 == ranges[3] && ranges[4] + 1 == ranges[5])
    {
    if (readch)
      read_char(common);
    if (ranges[1] != 0)
      {
      add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, ranges[2]));
      add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, ranges[4]));
      }
    else
      {
      jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, ranges[2]);
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, ranges[4]));
      JUMPHERE(jump);
      }
    return TRUE;
    }
  if ((ranges[3] - ranges[2]) == (ranges[5] - ranges[4]) && is_powerof2(ranges[4] - ranges[2]))
    {
    if (readch)
      read_char(common);
    OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, ranges[4] - ranges[2]);
    OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, ranges[4]);
    add_jump(compiler, backtracks, CMP(ranges[1] != 0 ? SLJIT_C_LESS : SLJIT_C_GREATER_EQUAL, TMP1, 0, SLJIT_IMM, ranges[5] - ranges[4]));
    return TRUE;
    }
  return FALSE;

  default:
  return FALSE;
  }
}

static void get_ctype_ranges(compiler_common *common, int flag, int *ranges)
{
int i, bit, length;
const pcre_uint8 *ctypes = (const pcre_uint8*)common->ctypes;

bit = ctypes[0] & flag;
ranges[0] = -1;
ranges[1] = bit != 0 ? 1 : 0;
length = 0;

for (i = 1; i < 256; i++)
  if ((ctypes[i] & flag) != bit)
    {
    if (length >= MAX_RANGE_SIZE)
      return;
    ranges[2 + length] = i;
    length++;
    bit ^= flag;
    }

if (bit != 0)
  {
  if (length >= MAX_RANGE_SIZE)
    return;
  ranges[2 + length] = 256;
  length++;
  }
ranges[0] = length;
}

static BOOL check_class_ranges(compiler_common *common, const pcre_uint8 *bits, BOOL nclass, jump_list **backtracks)
{
int ranges[2 + MAX_RANGE_SIZE];
pcre_uint8 bit, cbit, all;
int i, byte, length = 0;

bit = bits[0] & 0x1;
ranges[1] = bit;
/* Can be 0 or 255. */
all = -bit;

for (i = 0; i < 256; )
  {
  byte = i >> 3;
  if ((i & 0x7) == 0 && bits[byte] == all)
    i += 8;
  else
    {
    cbit = (bits[byte] >> (i & 0x7)) & 0x1;
    if (cbit != bit)
      {
      if (length >= MAX_RANGE_SIZE)
        return FALSE;
      ranges[2 + length] = i;
      length++;
      bit = cbit;
      all = -cbit;
      }
    i++;
    }
  }

if (((bit == 0) && nclass) || ((bit == 1) && !nclass))
  {
  if (length >= MAX_RANGE_SIZE)
    return FALSE;
  ranges[2 + length] = 256;
  length++;
  }
ranges[0] = length;

return check_ranges(common, ranges, backtracks, FALSE);
}

static void check_anynewline(compiler_common *common)
{
/* Check whether TMP1 contains a newline character. TMP2 destroyed. */
DEFINE_COMPILER;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);

OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x0a);
OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x0d - 0x0a);
OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS_EQUAL);
OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x85 - 0x0a);
#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
#ifdef COMPILE_PCRE8
if (common->utf)
  {
#endif
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x1);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x2029 - 0x0a);
#ifdef COMPILE_PCRE8
  }
#endif
#endif /* SUPPORT_UTF || COMPILE_PCRE16 || COMPILE_PCRE32 */
OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

static void check_hspace(compiler_common *common)
{
/* Check whether TMP1 contains a newline character. TMP2 destroyed. */
DEFINE_COMPILER;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);

OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x09);
OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x20);
OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0xa0);
#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
#ifdef COMPILE_PCRE8
if (common->utf)
  {
#endif
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x1680);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x180e);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x2000);
  OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x200A - 0x2000);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x202f - 0x2000);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x205f - 0x2000);
  OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x3000 - 0x2000);
#ifdef COMPILE_PCRE8
  }
#endif
#endif /* SUPPORT_UTF || COMPILE_PCRE16 || COMPILE_PCRE32 */
OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);

sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

static void check_vspace(compiler_common *common)
{
/* Check whether TMP1 contains a newline character. TMP2 destroyed. */
DEFINE_COMPILER;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);

OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x0a);
OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x0d - 0x0a);
OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS_EQUAL);
OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x85 - 0x0a);
#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
#ifdef COMPILE_PCRE8
if (common->utf)
  {
#endif
  OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, 0x1);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0x2029 - 0x0a);
#ifdef COMPILE_PCRE8
  }
#endif
#endif /* SUPPORT_UTF || COMPILE_PCRE16 || COMPILE_PCRE32 */
OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);

sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

#define CHAR1 STR_END
#define CHAR2 STACK_TOP

static void do_casefulcmp(compiler_common *common)
{
DEFINE_COMPILER;
struct sljit_jump *jump;
struct sljit_label *label;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, TMP2, 0);
OP1(SLJIT_MOV, TMP3, 0, CHAR1, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, CHAR2, 0);
OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));

label = LABEL();
OP1(MOVU_UCHAR, CHAR1, 0, SLJIT_MEM1(TMP1), IN_UCHARS(1));
OP1(MOVU_UCHAR, CHAR2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
jump = CMP(SLJIT_C_NOT_EQUAL, CHAR1, 0, CHAR2, 0);
OP2(SLJIT_SUB | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_IMM, IN_UCHARS(1));
JUMPTO(SLJIT_C_NOT_ZERO, label);

JUMPHERE(jump);
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
OP1(SLJIT_MOV, CHAR1, 0, TMP3, 0);
OP1(SLJIT_MOV, CHAR2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

#define LCC_TABLE STACK_LIMIT

static void do_caselesscmp(compiler_common *common)
{
DEFINE_COMPILER;
struct sljit_jump *jump;
struct sljit_label *label;

sljit_emit_fast_enter(compiler, RETURN_ADDR, 0);
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, TMP2, 0);

OP1(SLJIT_MOV, TMP3, 0, LCC_TABLE, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, CHAR1, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, CHAR2, 0);
OP1(SLJIT_MOV, LCC_TABLE, 0, SLJIT_IMM, common->lcc);
OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, IN_UCHARS(1));
OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));

label = LABEL();
OP1(MOVU_UCHAR, CHAR1, 0, SLJIT_MEM1(TMP1), IN_UCHARS(1));
OP1(MOVU_UCHAR, CHAR2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
#ifndef COMPILE_PCRE8
jump = CMP(SLJIT_C_GREATER, CHAR1, 0, SLJIT_IMM, 255);
#endif
OP1(SLJIT_MOV_UB, CHAR1, 0, SLJIT_MEM2(LCC_TABLE, CHAR1), 0);
#ifndef COMPILE_PCRE8
JUMPHERE(jump);
jump = CMP(SLJIT_C_GREATER, CHAR2, 0, SLJIT_IMM, 255);
#endif
OP1(SLJIT_MOV_UB, CHAR2, 0, SLJIT_MEM2(LCC_TABLE, CHAR2), 0);
#ifndef COMPILE_PCRE8
JUMPHERE(jump);
#endif
jump = CMP(SLJIT_C_NOT_EQUAL, CHAR1, 0, CHAR2, 0);
OP2(SLJIT_SUB | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_IMM, IN_UCHARS(1));
JUMPTO(SLJIT_C_NOT_ZERO, label);

JUMPHERE(jump);
OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
OP1(SLJIT_MOV, LCC_TABLE, 0, TMP3, 0);
OP1(SLJIT_MOV, CHAR1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
OP1(SLJIT_MOV, CHAR2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1);
sljit_emit_fast_return(compiler, RETURN_ADDR, 0);
}

#undef LCC_TABLE
#undef CHAR1
#undef CHAR2

#if defined SUPPORT_UTF && defined SUPPORT_UCP

static const pcre_uchar *SLJIT_CALL do_utf_caselesscmp(pcre_uchar *src1, jit_arguments *args, pcre_uchar *end1)
{
/* This function would be ineffective to do in JIT level. */
pcre_uint32 c1, c2;
const pcre_uchar *src2 = args->uchar_ptr;
const pcre_uchar *end2 = args->end;
const ucd_record *ur;
const pcre_uint32 *pp;

while (src1 < end1)
  {
  if (src2 >= end2)
    return (pcre_uchar*)1;
  GETCHARINC(c1, src1);
  GETCHARINC(c2, src2);
  ur = GET_UCD(c2);
  if (c1 != c2 && c1 != c2 + ur->other_case)
    {
    pp = PRIV(ucd_caseless_sets) + ur->caseset;
    for (;;)
      {
      if (c1 < *pp) return NULL;
      if (c1 == *pp++) break;
      }
    }
  }
return src2;
}

#endif /* SUPPORT_UTF && SUPPORT_UCP */

static pcre_uchar *byte_sequence_compare(compiler_common *common, BOOL caseless, pcre_uchar *cc,
    compare_context* context, jump_list **backtracks)
{
DEFINE_COMPILER;
unsigned int othercasebit = 0;
pcre_uchar *othercasechar = NULL;
#ifdef SUPPORT_UTF
int utflength;
#endif

if (caseless && char_has_othercase(common, cc))
  {
  othercasebit = char_get_othercase_bit(common, cc);
  SLJIT_ASSERT(othercasebit);
  /* Extracting bit difference info. */
#if defined COMPILE_PCRE8
  othercasechar = cc + (othercasebit >> 8);
  othercasebit &= 0xff;
#elif defined COMPILE_PCRE16 || defined COMPILE_PCRE32
  /* Note that this code only handles characters in the BMP. If there
  ever are characters outside the BMP whose othercase differs in only one
  bit from itself (there currently are none), this code will need to be
  revised for COMPILE_PCRE32. */
  othercasechar = cc + (othercasebit >> 9);
  if ((othercasebit & 0x100) != 0)
    othercasebit = (othercasebit & 0xff) << 8;
  else
    othercasebit &= 0xff;
#endif /* COMPILE_PCRE[8|16|32] */
  }

if (context->sourcereg == -1)
  {
#if defined COMPILE_PCRE8
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED
  if (context->length >= 4)
    OP1(SLJIT_MOV_SI, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
  else if (context->length >= 2)
    OP1(SLJIT_MOV_UH, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
  else
#endif
    OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
#elif defined COMPILE_PCRE16
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED
  if (context->length >= 4)
    OP1(SLJIT_MOV_SI, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
  else
#endif
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
#elif defined COMPILE_PCRE32
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), -context->length);
#endif /* COMPILE_PCRE[8|16|32] */
  context->sourcereg = TMP2;
  }

#ifdef SUPPORT_UTF
utflength = 1;
if (common->utf && HAS_EXTRALEN(*cc))
  utflength += GET_EXTRALEN(*cc);

do
  {
#endif

  context->length -= IN_UCHARS(1);
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED

  /* Unaligned read is supported. */
  if (othercasebit != 0 && othercasechar == cc)
    {
    context->c.asuchars[context->ucharptr] = *cc | othercasebit;
    context->oc.asuchars[context->ucharptr] = othercasebit;
    }
  else
    {
    context->c.asuchars[context->ucharptr] = *cc;
    context->oc.asuchars[context->ucharptr] = 0;
    }
  context->ucharptr++;

#if defined COMPILE_PCRE8
  if (context->ucharptr >= 4 || context->length == 0 || (context->ucharptr == 2 && context->length == 1))
#elif defined COMPILE_PCRE16
  if (context->ucharptr >= 2 || context->length == 0)
#elif defined COMPILE_PCRE32
  if (1 /* context->ucharptr >= 1 || context->length == 0 */)
#endif
    {
#if defined COMPILE_PCRE8 || defined COMPILE_PCRE16
    if (context->length >= 4)
      OP1(SLJIT_MOV_SI, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);
#if defined COMPILE_PCRE8
    else if (context->length >= 2)
      OP1(SLJIT_MOV_UH, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);
    else if (context->length >= 1)
      OP1(SLJIT_MOV_UB, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);
#elif defined COMPILE_PCRE16
    else if (context->length >= 2)
      OP1(SLJIT_MOV_UH, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);
#endif /* COMPILE_PCRE[8|16] */
#elif defined COMPILE_PCRE32
    OP1(MOV_UCHAR, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);
#endif /* COMPILE_PCRE[8|16|32] */
    context->sourcereg = context->sourcereg == TMP1 ? TMP2 : TMP1;

    switch(context->ucharptr)
      {
      case 4 / sizeof(pcre_uchar):
      if (context->oc.asint != 0)
        OP2(SLJIT_OR, context->sourcereg, 0, context->sourcereg, 0, SLJIT_IMM, context->oc.asint);
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, context->sourcereg, 0, SLJIT_IMM, context->c.asint | context->oc.asint));
      break;

#if defined COMPILE_PCRE8 || defined COMPILE_PCRE16
      case 2 / sizeof(pcre_uchar):
      if (context->oc.asushort != 0)
        OP2(SLJIT_OR, context->sourcereg, 0, context->sourcereg, 0, SLJIT_IMM, context->oc.asushort);
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, context->sourcereg, 0, SLJIT_IMM, context->c.asushort | context->oc.asushort));
      break;

#ifdef COMPILE_PCRE8
      case 1:
      if (context->oc.asbyte != 0)
        OP2(SLJIT_OR, context->sourcereg, 0, context->sourcereg, 0, SLJIT_IMM, context->oc.asbyte);
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, context->sourcereg, 0, SLJIT_IMM, context->c.asbyte | context->oc.asbyte));
      break;
#endif

#endif /* COMPILE_PCRE[8|16] */

      default:
      SLJIT_ASSERT_STOP();
      break;
      }
    context->ucharptr = 0;
    }

#else

  /* Unaligned read is unsupported. */
  if (context->length > 0)
    OP1(MOV_UCHAR, context->sourcereg, 0, SLJIT_MEM1(STR_PTR), -context->length);

  context->sourcereg = context->sourcereg == TMP1 ? TMP2 : TMP1;

  if (othercasebit != 0 && othercasechar == cc)
    {
    OP2(SLJIT_OR, context->sourcereg, 0, context->sourcereg, 0, SLJIT_IMM, othercasebit);
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, context->sourcereg, 0, SLJIT_IMM, *cc | othercasebit));
    }
  else
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, context->sourcereg, 0, SLJIT_IMM, *cc));

#endif

  cc++;
#ifdef SUPPORT_UTF
  utflength--;
  }
while (utflength > 0);
#endif

return cc;
}

#if defined SUPPORT_UTF || !defined COMPILE_PCRE8

#define SET_TYPE_OFFSET(value) \
  if ((value) != typeoffset) \
    { \
    if ((value) > typeoffset) \
      OP2(SLJIT_SUB, typereg, 0, typereg, 0, SLJIT_IMM, (value) - typeoffset); \
    else \
      OP2(SLJIT_ADD, typereg, 0, typereg, 0, SLJIT_IMM, typeoffset - (value)); \
    } \
  typeoffset = (value);

#define SET_CHAR_OFFSET(value) \
  if ((value) != charoffset) \
    { \
    if ((value) > charoffset) \
      OP2(SLJIT_SUB, TMP1, 0, TMP1, 0, SLJIT_IMM, (value) - charoffset); \
    else \
      OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, charoffset - (value)); \
    } \
  charoffset = (value);

static void compile_xclass_matchingpath(compiler_common *common, pcre_uchar *cc, jump_list **backtracks)
{
DEFINE_COMPILER;
jump_list *found = NULL;
jump_list **list = (*cc & XCL_NOT) == 0 ? &found : backtracks;
pcre_int32 c, charoffset;
const pcre_uint32 *other_cases;
struct sljit_jump *jump = NULL;
pcre_uchar *ccbegin;
int compares, invertcmp, numberofcmps;
#ifdef SUPPORT_UCP
BOOL needstype = FALSE, needsscript = FALSE, needschar = FALSE;
BOOL charsaved = FALSE;
int typereg = TMP1, scriptreg = TMP1;
pcre_int32 typeoffset;
#endif

(void)other_cases;
/* Although SUPPORT_UTF must be defined, we are
   not necessary in utf mode even in 8 bit mode. */
detect_partial_match(common, backtracks);
read_char(common);

if ((*cc++ & XCL_MAP) != 0)
  {
  OP1(SLJIT_MOV, TMP3, 0, TMP1, 0);
#ifndef COMPILE_PCRE8
  jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#elif defined SUPPORT_UTF
  if (common->utf)
    jump = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
#endif

  if (!check_class_ranges(common, (const pcre_uint8 *)cc, TRUE, list))
    {
    OP2(SLJIT_AND, TMP2, 0, TMP1, 0, SLJIT_IMM, 0x7);
    OP2(SLJIT_LSHR, TMP1, 0, TMP1, 0, SLJIT_IMM, 3);
    OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)cc);
    OP2(SLJIT_SHL, TMP2, 0, SLJIT_IMM, 1, TMP2, 0);
    OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, TMP2, 0);
    add_jump(compiler, list, JUMP(SLJIT_C_NOT_ZERO));
    }

#ifndef COMPILE_PCRE8
  JUMPHERE(jump);
#elif defined SUPPORT_UTF
  if (common->utf)
    JUMPHERE(jump);
#endif
  OP1(SLJIT_MOV, TMP1, 0, TMP3, 0);
#ifdef SUPPORT_UCP
  charsaved = TRUE;
#endif
  cc += 32 / sizeof(pcre_uchar);
  }

/* Scanning the necessary info. */
ccbegin = cc;
compares = 0;
while (*cc != XCL_END)
  {
  compares++;
  if (*cc == XCL_SINGLE)
    {
    cc += 2;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
#ifdef SUPPORT_UCP
    needschar = TRUE;
#endif
    }
  else if (*cc == XCL_RANGE)
    {
    cc += 2;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
    cc++;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[-1])) cc += GET_EXTRALEN(cc[-1]);
#endif
#ifdef SUPPORT_UCP
    needschar = TRUE;
#endif
    }
#ifdef SUPPORT_UCP
  else
    {
    SLJIT_ASSERT(*cc == XCL_PROP || *cc == XCL_NOTPROP);
    cc++;
    switch(*cc)
      {
      case PT_ANY:
      break;

      case PT_LAMP:
      case PT_GC:
      case PT_PC:
      case PT_ALNUM:
      needstype = TRUE;
      break;

      case PT_SC:
      needsscript = TRUE;
      break;

      case PT_SPACE:
      case PT_PXSPACE:
      case PT_WORD:
      needstype = TRUE;
      needschar = TRUE;
      break;

      case PT_CLIST:
      needschar = TRUE;
      break;

      default:
      SLJIT_ASSERT_STOP();
      break;
      }
    cc += 2;
    }
#endif
  }

#ifdef SUPPORT_UCP
/* Simple register allocation. TMP1 is preferred if possible. */
if (needstype || needsscript)
  {
  if (needschar && !charsaved)
    OP1(SLJIT_MOV, TMP3, 0, TMP1, 0);
  add_jump(compiler, &common->getucd, JUMP(SLJIT_FAST_CALL));
  if (needschar)
    {
    if (needstype)
      {
      OP1(SLJIT_MOV, RETURN_ADDR, 0, TMP1, 0);
      typereg = RETURN_ADDR;
      }

    if (needsscript)
      scriptreg = TMP3;
    OP1(SLJIT_MOV, TMP1, 0, TMP3, 0);
    }
  else if (needstype && needsscript)
    scriptreg = TMP3;
  /* In all other cases only one of them was specified, and that can goes to TMP1. */

  if (needsscript)
    {
    if (scriptreg == TMP1)
      {
      OP1(SLJIT_MOV, scriptreg, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_records) + SLJIT_OFFSETOF(ucd_record, script));
      OP1(SLJIT_MOV_UB, scriptreg, 0, SLJIT_MEM2(scriptreg, TMP2), 3);
      }
    else
      {
      OP2(SLJIT_SHL, TMP2, 0, TMP2, 0, SLJIT_IMM, 3);
      OP2(SLJIT_ADD, TMP2, 0, TMP2, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_records) + SLJIT_OFFSETOF(ucd_record, script));
      OP1(SLJIT_MOV_UB, scriptreg, 0, SLJIT_MEM1(TMP2), 0);
      }
    }
  }
#endif

/* Generating code. */
cc = ccbegin;
charoffset = 0;
numberofcmps = 0;
#ifdef SUPPORT_UCP
typeoffset = 0;
#endif

while (*cc != XCL_END)
  {
  compares--;
  invertcmp = (compares == 0 && list != backtracks);
  jump = NULL;

  if (*cc == XCL_SINGLE)
    {
    cc ++;
#ifdef SUPPORT_UTF
    if (common->utf)
      {
      GETCHARINC(c, cc);
      }
    else
#endif
      c = *cc++;

    if (numberofcmps < 3 && (*cc == XCL_SINGLE || *cc == XCL_RANGE))
      {
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, c - charoffset);
      OP_FLAGS(numberofcmps == 0 ? SLJIT_MOV : SLJIT_OR, TMP2, 0, numberofcmps == 0 ? SLJIT_UNUSED : TMP2, 0, SLJIT_C_EQUAL);
      numberofcmps++;
      }
    else if (numberofcmps > 0)
      {
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, c - charoffset);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      numberofcmps = 0;
      }
    else
      {
      jump = CMP(SLJIT_C_EQUAL ^ invertcmp, TMP1, 0, SLJIT_IMM, c - charoffset);
      numberofcmps = 0;
      }
    }
  else if (*cc == XCL_RANGE)
    {
    cc ++;
#ifdef SUPPORT_UTF
    if (common->utf)
      {
      GETCHARINC(c, cc);
      }
    else
#endif
      c = *cc++;
    SET_CHAR_OFFSET(c);
#ifdef SUPPORT_UTF
    if (common->utf)
      {
      GETCHARINC(c, cc);
      }
    else
#endif
      c = *cc++;
    if (numberofcmps < 3 && (*cc == XCL_SINGLE || *cc == XCL_RANGE))
      {
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, c - charoffset);
      OP_FLAGS(numberofcmps == 0 ? SLJIT_MOV : SLJIT_OR, TMP2, 0, numberofcmps == 0 ? SLJIT_UNUSED : TMP2, 0, SLJIT_C_LESS_EQUAL);
      numberofcmps++;
      }
    else if (numberofcmps > 0)
      {
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, c - charoffset);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      numberofcmps = 0;
      }
    else
      {
      jump = CMP(SLJIT_C_LESS_EQUAL ^ invertcmp, TMP1, 0, SLJIT_IMM, c - charoffset);
      numberofcmps = 0;
      }
    }
#ifdef SUPPORT_UCP
  else
    {
    if (*cc == XCL_NOTPROP)
      invertcmp ^= 0x1;
    cc++;
    switch(*cc)
      {
      case PT_ANY:
      if (list != backtracks)
        {
        if ((cc[-1] == XCL_NOTPROP && compares > 0) || (cc[-1] == XCL_PROP && compares == 0))
          continue;
        }
      else if (cc[-1] == XCL_NOTPROP)
        continue;
      jump = JUMP(SLJIT_JUMP);
      break;

      case PT_LAMP:
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_Lu - typeoffset);
      OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_Ll - typeoffset);
      OP_FLAGS(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_Lt - typeoffset);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      break;

      case PT_GC:
      c = PRIV(ucp_typerange)[(int)cc[1] * 2];
      SET_TYPE_OFFSET(c);
      jump = CMP(SLJIT_C_LESS_EQUAL ^ invertcmp, typereg, 0, SLJIT_IMM, PRIV(ucp_typerange)[(int)cc[1] * 2 + 1] - c);
      break;

      case PT_PC:
      jump = CMP(SLJIT_C_EQUAL ^ invertcmp, typereg, 0, SLJIT_IMM, (int)cc[1] - typeoffset);
      break;

      case PT_SC:
      jump = CMP(SLJIT_C_EQUAL ^ invertcmp, scriptreg, 0, SLJIT_IMM, (int)cc[1]);
      break;

      case PT_SPACE:
      case PT_PXSPACE:
      if (*cc == PT_SPACE)
        {
        OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, 0);
        jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, 11 - charoffset);
        }
      SET_CHAR_OFFSET(9);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 13 - 9);
      OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS_EQUAL);
      if (*cc == PT_SPACE)
        JUMPHERE(jump);

      SET_TYPE_OFFSET(ucp_Zl);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_Zs - ucp_Zl);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      break;

      case PT_WORD:
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, CHAR_UNDERSCORE - charoffset);
      OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
      /* ... fall through */

      case PT_ALNUM:
      SET_TYPE_OFFSET(ucp_Ll);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_Lu - ucp_Ll);
      OP_FLAGS((*cc == PT_ALNUM) ? SLJIT_MOV : SLJIT_OR, TMP2, 0, (*cc == PT_ALNUM) ? SLJIT_UNUSED : TMP2, 0, SLJIT_C_LESS_EQUAL);
      SET_TYPE_OFFSET(ucp_Nd);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, typereg, 0, SLJIT_IMM, ucp_No - ucp_Nd);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_LESS_EQUAL);
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      break;

      case PT_CLIST:
      other_cases = PRIV(ucd_caseless_sets) + cc[1];

      /* At least three characters are required.
         Otherwise this case would be handled by the normal code path. */
      SLJIT_ASSERT(other_cases[0] != NOTACHAR && other_cases[1] != NOTACHAR && other_cases[2] != NOTACHAR);
      SLJIT_ASSERT(other_cases[0] < other_cases[1] && other_cases[1] < other_cases[2]);

      /* Optimizing character pairs, if their difference is power of 2. */
      if (is_powerof2(other_cases[1] ^ other_cases[0]))
        {
        if (charoffset == 0)
          OP2(SLJIT_OR, TMP2, 0, TMP1, 0, SLJIT_IMM, other_cases[1] ^ other_cases[0]);
        else
          {
          OP2(SLJIT_ADD, TMP2, 0, TMP1, 0, SLJIT_IMM, (sljit_sw)charoffset);
          OP2(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_IMM, other_cases[1] ^ other_cases[0]);
          }
        OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP2, 0, SLJIT_IMM, other_cases[1]);
        OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
        other_cases += 2;
        }
      else if (is_powerof2(other_cases[2] ^ other_cases[1]))
        {
        if (charoffset == 0)
          OP2(SLJIT_OR, TMP2, 0, TMP1, 0, SLJIT_IMM, other_cases[2] ^ other_cases[1]);
        else
          {
          OP2(SLJIT_ADD, TMP2, 0, TMP1, 0, SLJIT_IMM, (sljit_sw)charoffset);
          OP2(SLJIT_OR, TMP2, 0, TMP2, 0, SLJIT_IMM, other_cases[1] ^ other_cases[0]);
          }
        OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP2, 0, SLJIT_IMM, other_cases[2]);
        OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);

        OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, other_cases[0] - charoffset);
        OP_FLAGS(SLJIT_OR | ((other_cases[3] == NOTACHAR) ? SLJIT_SET_E : 0), TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);

        other_cases += 3;
        }
      else
        {
        OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, *other_cases++ - charoffset);
        OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
        }

      while (*other_cases != NOTACHAR)
        {
        OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, *other_cases++ - charoffset);
        OP_FLAGS(SLJIT_OR | ((*other_cases == NOTACHAR) ? SLJIT_SET_E : 0), TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
        }
      jump = JUMP(SLJIT_C_NOT_ZERO ^ invertcmp);
      break;
      }
    cc += 2;
    }
#endif

  if (jump != NULL)
    add_jump(compiler, compares > 0 ? list : backtracks, jump);
  }

if (found != NULL)
  set_jumps(found, LABEL());
}

#undef SET_TYPE_OFFSET
#undef SET_CHAR_OFFSET

#endif

static pcre_uchar *compile_char1_matchingpath(compiler_common *common, pcre_uchar type, pcre_uchar *cc, jump_list **backtracks)
{
DEFINE_COMPILER;
int length;
unsigned int c, oc, bit;
compare_context context;
struct sljit_jump *jump[4];
#ifdef SUPPORT_UTF
struct sljit_label *label;
#ifdef SUPPORT_UCP
pcre_uchar propdata[5];
#endif
#endif

switch(type)
  {
  case OP_SOD:
  OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, begin));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, TMP1, 0));
  return cc;

  case OP_SOM:
  OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, TMP1, 0));
  return cc;

  case OP_NOT_WORD_BOUNDARY:
  case OP_WORD_BOUNDARY:
  add_jump(compiler, &common->wordboundary, JUMP(SLJIT_FAST_CALL));
  add_jump(compiler, backtracks, JUMP(type == OP_NOT_WORD_BOUNDARY ? SLJIT_C_NOT_ZERO : SLJIT_C_ZERO));
  return cc;

  case OP_NOT_DIGIT:
  case OP_DIGIT:
  /* Digits are usually 0-9, so it is worth to optimize them. */
  if (common->digits[0] == -2)
    get_ctype_ranges(common, ctype_digit, common->digits);
  detect_partial_match(common, backtracks);
  /* Flip the starting bit in the negative case. */
  if (type == OP_NOT_DIGIT)
    common->digits[1] ^= 1;
  if (!check_ranges(common, common->digits, backtracks, TRUE))
    {
    read_char8_type(common);
    OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ctype_digit);
    add_jump(compiler, backtracks, JUMP(type == OP_DIGIT ? SLJIT_C_ZERO : SLJIT_C_NOT_ZERO));
    }
  if (type == OP_NOT_DIGIT)
    common->digits[1] ^= 1;
  return cc;

  case OP_NOT_WHITESPACE:
  case OP_WHITESPACE:
  detect_partial_match(common, backtracks);
  read_char8_type(common);
  OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ctype_space);
  add_jump(compiler, backtracks, JUMP(type == OP_WHITESPACE ? SLJIT_C_ZERO : SLJIT_C_NOT_ZERO));
  return cc;

  case OP_NOT_WORDCHAR:
  case OP_WORDCHAR:
  detect_partial_match(common, backtracks);
  read_char8_type(common);
  OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, ctype_word);
  add_jump(compiler, backtracks, JUMP(type == OP_WORDCHAR ? SLJIT_C_ZERO : SLJIT_C_NOT_ZERO));
  return cc;

  case OP_ANY:
  detect_partial_match(common, backtracks);
  read_char(common);
  if (common->nltype == NLTYPE_FIXED && common->newline > 255)
    {
    jump[0] = CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff);
    if (common->mode != JIT_PARTIAL_HARD_COMPILE)
      jump[1] = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
    else
      jump[1] = check_str_end(common);

    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
    add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, common->newline & 0xff));
    if (jump[1] != NULL)
      JUMPHERE(jump[1]);
    JUMPHERE(jump[0]);
    }
  else
    check_newlinechar(common, common->nltype, backtracks, TRUE);
  return cc;

  case OP_ALLANY:
  detect_partial_match(common, backtracks);
#ifdef SUPPORT_UTF
  if (common->utf)
    {
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
    OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
#if defined COMPILE_PCRE8 || defined COMPILE_PCRE16
#if defined COMPILE_PCRE8
    jump[0] = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0);
    OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)PRIV(utf8_table4) - 0xc0);
    OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
#elif defined COMPILE_PCRE16
    jump[0] = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xd800);
    OP2(SLJIT_AND, TMP1, 0, TMP1, 0, SLJIT_IMM, 0xfc00);
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, 0xd800);
    OP_FLAGS(SLJIT_MOV, TMP1, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
    OP2(SLJIT_SHL, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
    OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
#endif
    JUMPHERE(jump[0]);
#endif /* COMPILE_PCRE[8|16] */
    return cc;
    }
#endif
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  return cc;

  case OP_ANYBYTE:
  detect_partial_match(common, backtracks);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  return cc;

#ifdef SUPPORT_UTF
#ifdef SUPPORT_UCP
  case OP_NOTPROP:
  case OP_PROP:
  propdata[0] = 0;
  propdata[1] = type == OP_NOTPROP ? XCL_NOTPROP : XCL_PROP;
  propdata[2] = cc[0];
  propdata[3] = cc[1];
  propdata[4] = XCL_END;
  compile_xclass_matchingpath(common, propdata, backtracks);
  return cc + 2;
#endif
#endif

  case OP_ANYNL:
  detect_partial_match(common, backtracks);
  read_char(common);
  jump[0] = CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_CR);
  /* We don't need to handle soft partial matching case. */
  if (common->mode != JIT_PARTIAL_HARD_COMPILE)
    jump[1] = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  else
    jump[1] = check_str_end(common);
  OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
  jump[2] = CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_NL);
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
  jump[3] = JUMP(SLJIT_JUMP);
  JUMPHERE(jump[0]);
  check_newlinechar(common, common->bsr_nltype, backtracks, FALSE);
  JUMPHERE(jump[1]);
  JUMPHERE(jump[2]);
  JUMPHERE(jump[3]);
  return cc;

  case OP_NOT_HSPACE:
  case OP_HSPACE:
  detect_partial_match(common, backtracks);
  read_char(common);
  add_jump(compiler, &common->hspace, JUMP(SLJIT_FAST_CALL));
  add_jump(compiler, backtracks, JUMP(type == OP_NOT_HSPACE ? SLJIT_C_NOT_ZERO : SLJIT_C_ZERO));
  return cc;

  case OP_NOT_VSPACE:
  case OP_VSPACE:
  detect_partial_match(common, backtracks);
  read_char(common);
  add_jump(compiler, &common->vspace, JUMP(SLJIT_FAST_CALL));
  add_jump(compiler, backtracks, JUMP(type == OP_NOT_VSPACE ? SLJIT_C_NOT_ZERO : SLJIT_C_ZERO));
  return cc;

#ifdef SUPPORT_UCP
  case OP_EXTUNI:
  detect_partial_match(common, backtracks);
  read_char(common);
  add_jump(compiler, &common->getucd, JUMP(SLJIT_FAST_CALL));
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_records) + SLJIT_OFFSETOF(ucd_record, gbprop));
  /* Optimize register allocation: use a real register. */
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, STACK_TOP, 0);
  OP1(SLJIT_MOV_UB, STACK_TOP, 0, SLJIT_MEM2(TMP1, TMP2), 3);

  label = LABEL();
  jump[0] = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  OP1(SLJIT_MOV, TMP3, 0, STR_PTR, 0);
  read_char(common);
  add_jump(compiler, &common->getucd, JUMP(SLJIT_FAST_CALL));
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_IMM, (sljit_sw)PRIV(ucd_records) + SLJIT_OFFSETOF(ucd_record, gbprop));
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM2(TMP1, TMP2), 3);

  OP2(SLJIT_SHL, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, 2);
  OP1(SLJIT_MOV_UI, TMP1, 0, SLJIT_MEM1(STACK_TOP), (sljit_sw)PRIV(ucp_gbtable));
  OP1(SLJIT_MOV, STACK_TOP, 0, TMP2, 0);
  OP2(SLJIT_SHL, TMP2, 0, SLJIT_IMM, 1, TMP2, 0);
  OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, TMP2, 0);
  JUMPTO(SLJIT_C_NOT_ZERO, label);

  OP1(SLJIT_MOV, STR_PTR, 0, TMP3, 0);
  JUMPHERE(jump[0]);
  OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);

  if (common->mode == JIT_PARTIAL_HARD_COMPILE)
    {
    jump[0] = CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0);
    /* Since we successfully read a char above, partial matching must occure. */
    check_partial(common, TRUE);
    JUMPHERE(jump[0]);
    }
  return cc;
#endif

  case OP_EODN:
  /* Requires rather complex checks. */
  jump[0] = CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0);
  if (common->nltype == NLTYPE_FIXED && common->newline > 255)
    {
    OP2(SLJIT_ADD, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(2));
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
    if (common->mode == JIT_COMPILE)
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_END, 0));
    else
      {
      jump[1] = CMP(SLJIT_C_EQUAL, TMP2, 0, STR_END, 0);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP2, 0, STR_END, 0);
      OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_LESS);
      OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_NOT_EQUAL);
      add_jump(compiler, backtracks, JUMP(SLJIT_C_NOT_EQUAL));
      check_partial(common, TRUE);
      add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));
      JUMPHERE(jump[1]);
      }
    OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, common->newline & 0xff));
    }
  else if (common->nltype == NLTYPE_FIXED)
    {
    OP2(SLJIT_ADD, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_END, 0));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, common->newline));
    }
  else
    {
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
    jump[1] = CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_CR);
    OP2(SLJIT_ADD, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(2));
    OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP2, 0, STR_END, 0);
    jump[2] = JUMP(SLJIT_C_GREATER);
    add_jump(compiler, backtracks, JUMP(SLJIT_C_LESS));
    /* Equal. */
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
    jump[3] = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_NL);
    add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));

    JUMPHERE(jump[1]);
    if (common->nltype == NLTYPE_ANYCRLF)
      {
      OP2(SLJIT_ADD, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
      add_jump(compiler, backtracks, CMP(SLJIT_C_LESS, TMP2, 0, STR_END, 0));
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, CHAR_NL));
      }
    else
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, STR_PTR, 0);
      read_char(common);
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, STR_END, 0));
      add_jump(compiler, &common->anynewline, JUMP(SLJIT_FAST_CALL));
      add_jump(compiler, backtracks, JUMP(SLJIT_C_ZERO));
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1);
      }
    JUMPHERE(jump[2]);
    JUMPHERE(jump[3]);
    }
  JUMPHERE(jump[0]);
  check_partial(common, FALSE);
  return cc;

  case OP_EOD:
  add_jump(compiler, backtracks, CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0));
  check_partial(common, FALSE);
  return cc;

  case OP_CIRC:
  OP1(SLJIT_MOV, TMP2, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, begin));
  add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER, STR_PTR, 0, TMP1, 0));
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, notbol));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));
  return cc;

  case OP_CIRCM:
  OP1(SLJIT_MOV, TMP2, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, begin));
  jump[1] = CMP(SLJIT_C_GREATER, STR_PTR, 0, TMP1, 0);
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, notbol));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));
  jump[0] = JUMP(SLJIT_JUMP);
  JUMPHERE(jump[1]);

  add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER_EQUAL, STR_PTR, 0, STR_END, 0));
  if (common->nltype == NLTYPE_FIXED && common->newline > 255)
    {
    OP2(SLJIT_SUB, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(2));
    add_jump(compiler, backtracks, CMP(SLJIT_C_LESS, TMP2, 0, TMP1, 0));
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(-2));
    OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(-1));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, common->newline & 0xff));
    }
  else
    {
    skip_char_back(common);
    read_char(common);
    check_newlinechar(common, common->nltype, backtracks, FALSE);
    }
  JUMPHERE(jump[0]);
  return cc;

  case OP_DOLL:
  OP1(SLJIT_MOV, TMP2, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, noteol));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));

  if (!common->endonly)
    compile_char1_matchingpath(common, OP_EODN, cc, backtracks);
  else
    {
    add_jump(compiler, backtracks, CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0));
    check_partial(common, FALSE);
    }
  return cc;

  case OP_DOLLM:
  jump[1] = CMP(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0);
  OP1(SLJIT_MOV, TMP2, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(jit_arguments, noteol));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));
  check_partial(common, FALSE);
  jump[0] = JUMP(SLJIT_JUMP);
  JUMPHERE(jump[1]);

  if (common->nltype == NLTYPE_FIXED && common->newline > 255)
    {
    OP2(SLJIT_ADD, TMP2, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(2));
    OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(0));
    if (common->mode == JIT_COMPILE)
      add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER, TMP2, 0, STR_END, 0));
    else
      {
      jump[1] = CMP(SLJIT_C_LESS_EQUAL, TMP2, 0, STR_END, 0);
      /* STR_PTR = STR_END - IN_UCHARS(1) */
      add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff));
      check_partial(common, TRUE);
      add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));
      JUMPHERE(jump[1]);
      }

    OP1(MOV_UCHAR, TMP2, 0, SLJIT_MEM1(STR_PTR), IN_UCHARS(1));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, (common->newline >> 8) & 0xff));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, common->newline & 0xff));
    }
  else
    {
    peek_char(common);
    check_newlinechar(common, common->nltype, backtracks, FALSE);
    }
  JUMPHERE(jump[0]);
  return cc;

  case OP_CHAR:
  case OP_CHARI:
  length = 1;
#ifdef SUPPORT_UTF
  if (common->utf && HAS_EXTRALEN(*cc)) length += GET_EXTRALEN(*cc);
#endif
  if (common->mode == JIT_COMPILE && (type == OP_CHAR || !char_has_othercase(common, cc) || char_get_othercase_bit(common, cc) != 0))
    {
    OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(length));
    add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER, STR_PTR, 0, STR_END, 0));

    context.length = IN_UCHARS(length);
    context.sourcereg = -1;
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED
    context.ucharptr = 0;
#endif
    return byte_sequence_compare(common, type == OP_CHARI, cc, &context, backtracks);
    }
  detect_partial_match(common, backtracks);
  read_char(common);
#ifdef SUPPORT_UTF
  if (common->utf)
    {
    GETCHAR(c, cc);
    }
  else
#endif
    c = *cc;
  if (type == OP_CHAR || !char_has_othercase(common, cc))
    {
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, c));
    return cc + length;
    }
  oc = char_othercase(common, c);
  bit = c ^ oc;
  if (is_powerof2(bit))
    {
    OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, bit);
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, c | bit));
    return cc + length;
    }
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, c);
  OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
  OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_IMM, oc);
  OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
  add_jump(compiler, backtracks, JUMP(SLJIT_C_ZERO));
  return cc + length;

  case OP_NOT:
  case OP_NOTI:
  detect_partial_match(common, backtracks);
  length = 1;
#ifdef SUPPORT_UTF
  if (common->utf)
    {
#ifdef COMPILE_PCRE8
    c = *cc;
    if (c < 128)
      {
      OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(STR_PTR), 0);
      if (type == OP_NOT || !char_has_othercase(common, cc))
        add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, c));
      else
        {
        /* Since UTF8 code page is fixed, we know that c is in [a-z] or [A-Z] range. */
        OP2(SLJIT_OR, TMP2, 0, TMP1, 0, SLJIT_IMM, 0x20);
        add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, c | 0x20));
        }
      /* Skip the variable-length character. */
      OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(1));
      jump[0] = CMP(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, 0xc0);
      OP1(MOV_UCHAR, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)PRIV(utf8_table4) - 0xc0);
      OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP1, 0);
      JUMPHERE(jump[0]);
      return cc + 1;
      }
    else
#endif /* COMPILE_PCRE8 */
      {
      GETCHARLEN(c, cc, length);
      read_char(common);
      }
    }
  else
#endif /* SUPPORT_UTF */
    {
    read_char(common);
    c = *cc;
    }

  if (type == OP_NOT || !char_has_othercase(common, cc))
    add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, c));
  else
    {
    oc = char_othercase(common, c);
    bit = c ^ oc;
    if (is_powerof2(bit))
      {
      OP2(SLJIT_OR, TMP1, 0, TMP1, 0, SLJIT_IMM, bit);
      add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, c | bit));
      }
    else
      {
      add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, c));
      add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, oc));
      }
    }
  return cc + length;

  case OP_CLASS:
  case OP_NCLASS:
  detect_partial_match(common, backtracks);
  read_char(common);
  if (check_class_ranges(common, (const pcre_uint8 *)cc, type == OP_NCLASS, backtracks))
    return cc + 32 / sizeof(pcre_uchar);

#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
  jump[0] = NULL;
#ifdef COMPILE_PCRE8
  /* This check only affects 8 bit mode. In other modes, we
  always need to compare the value with 255. */
  if (common->utf)
#endif /* COMPILE_PCRE8 */
    {
    jump[0] = CMP(SLJIT_C_GREATER, TMP1, 0, SLJIT_IMM, 255);
    if (type == OP_CLASS)
      {
      add_jump(compiler, backtracks, jump[0]);
      jump[0] = NULL;
      }
    }
#endif /* SUPPORT_UTF || !COMPILE_PCRE8 */
  OP2(SLJIT_AND, TMP2, 0, TMP1, 0, SLJIT_IMM, 0x7);
  OP2(SLJIT_LSHR, TMP1, 0, TMP1, 0, SLJIT_IMM, 3);
  OP1(SLJIT_MOV_UB, TMP1, 0, SLJIT_MEM1(TMP1), (sljit_sw)cc);
  OP2(SLJIT_SHL, TMP2, 0, SLJIT_IMM, 1, TMP2, 0);
  OP2(SLJIT_AND | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, TMP2, 0);
  add_jump(compiler, backtracks, JUMP(SLJIT_C_ZERO));
#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
  if (jump[0] != NULL)
    JUMPHERE(jump[0]);
#endif /* SUPPORT_UTF || !COMPILE_PCRE8 */
  return cc + 32 / sizeof(pcre_uchar);

#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
  case OP_XCLASS:
  compile_xclass_matchingpath(common, cc + LINK_SIZE, backtracks);
  return cc + GET(cc, 0) - 1;
#endif

  case OP_REVERSE:
  length = GET(cc, 0);
  if (length == 0)
    return cc + LINK_SIZE;
  OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
#ifdef SUPPORT_UTF
  if (common->utf)
    {
    OP1(SLJIT_MOV, TMP3, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, begin));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, length);
    label = LABEL();
    add_jump(compiler, backtracks, CMP(SLJIT_C_LESS_EQUAL, STR_PTR, 0, TMP3, 0));
    skip_char_back(common);
    OP2(SLJIT_SUB | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_IMM, 1);
    JUMPTO(SLJIT_C_NOT_ZERO, label);
    }
  else
#endif
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, begin));
    OP2(SLJIT_SUB, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(length));
    add_jump(compiler, backtracks, CMP(SLJIT_C_LESS, STR_PTR, 0, TMP1, 0));
    }
  check_start_used_ptr(common);
  return cc + LINK_SIZE;
  }
SLJIT_ASSERT_STOP();
return cc;
}

static SLJIT_INLINE pcre_uchar *compile_charn_matchingpath(compiler_common *common, pcre_uchar *cc, pcre_uchar *ccend, jump_list **backtracks)
{
/* This function consumes at least one input character. */
/* To decrease the number of length checks, we try to concatenate the fixed length character sequences. */
DEFINE_COMPILER;
pcre_uchar *ccbegin = cc;
compare_context context;
int size;

context.length = 0;
do
  {
  if (cc >= ccend)
    break;

  if (*cc == OP_CHAR)
    {
    size = 1;
#ifdef SUPPORT_UTF
    if (common->utf && HAS_EXTRALEN(cc[1]))
      size += GET_EXTRALEN(cc[1]);
#endif
    }
  else if (*cc == OP_CHARI)
    {
    size = 1;
#ifdef SUPPORT_UTF
    if (common->utf)
      {
      if (char_has_othercase(common, cc + 1) && char_get_othercase_bit(common, cc + 1) == 0)
        size = 0;
      else if (HAS_EXTRALEN(cc[1]))
        size += GET_EXTRALEN(cc[1]);
      }
    else
#endif
    if (char_has_othercase(common, cc + 1) && char_get_othercase_bit(common, cc + 1) == 0)
      size = 0;
    }
  else
    size = 0;

  cc += 1 + size;
  context.length += IN_UCHARS(size);
  }
while (size > 0 && context.length <= 128);

cc = ccbegin;
if (context.length > 0)
  {
  /* We have a fixed-length byte sequence. */
  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, SLJIT_IMM, context.length);
  add_jump(compiler, backtracks, CMP(SLJIT_C_GREATER, STR_PTR, 0, STR_END, 0));

  context.sourcereg = -1;
#if defined SLJIT_UNALIGNED && SLJIT_UNALIGNED
  context.ucharptr = 0;
#endif
  do cc = byte_sequence_compare(common, *cc == OP_CHARI, cc + 1, &context, backtracks); while (context.length > 0);
  return cc;
  }

/* A non-fixed length character will be checked if length == 0. */
return compile_char1_matchingpath(common, *cc, cc + 1, backtracks);
}

static struct sljit_jump *compile_ref_checks(compiler_common *common, pcre_uchar *cc, jump_list **backtracks)
{
DEFINE_COMPILER;
int offset = GET2(cc, 1) << 1;

OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset));
if (!common->jscript_compat)
  {
  if (backtracks == NULL)
    {
    /* OVECTOR(1) contains the "string begin - 1" constant. */
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1));
    OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_EQUAL);
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_UNUSED, 0, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
    OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_EQUAL);
    return JUMP(SLJIT_C_NOT_ZERO);
    }
  add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1)));
  }
return CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
}

/* Forward definitions. */
static void compile_matchingpath(compiler_common *, pcre_uchar *, pcre_uchar *, backtrack_common *);
static void compile_backtrackingpath(compiler_common *, struct backtrack_common *);

#define PUSH_BACKTRACK(size, ccstart, error) \
  do \
    { \
    backtrack = sljit_alloc_memory(compiler, (size)); \
    if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler))) \
      return error; \
    memset(backtrack, 0, size); \
    backtrack->prev = parent->top; \
    backtrack->cc = (ccstart); \
    parent->top = backtrack; \
    } \
  while (0)

#define PUSH_BACKTRACK_NOVALUE(size, ccstart) \
  do \
    { \
    backtrack = sljit_alloc_memory(compiler, (size)); \
    if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler))) \
      return; \
    memset(backtrack, 0, size); \
    backtrack->prev = parent->top; \
    backtrack->cc = (ccstart); \
    parent->top = backtrack; \
    } \
  while (0)

#define BACKTRACK_AS(type) ((type *)backtrack)

static pcre_uchar *compile_ref_matchingpath(compiler_common *common, pcre_uchar *cc, jump_list **backtracks, BOOL withchecks, BOOL emptyfail)
{
DEFINE_COMPILER;
int offset = GET2(cc, 1) << 1;
struct sljit_jump *jump = NULL;
struct sljit_jump *partial;
struct sljit_jump *nopartial;

OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset));
/* OVECTOR(1) contains the "string begin - 1" constant. */
if (withchecks && !common->jscript_compat)
  add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1)));

#if defined SUPPORT_UTF && defined SUPPORT_UCP
if (common->utf && *cc == OP_REFI)
  {
  SLJIT_ASSERT(TMP1 == SLJIT_SCRATCH_REG1 && STACK_TOP == SLJIT_SCRATCH_REG2 && TMP2 == SLJIT_SCRATCH_REG3);
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
  if (withchecks)
    jump = CMP(SLJIT_C_EQUAL, TMP1, 0, TMP2, 0);

  /* Needed to save important temporary registers. */
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, STACK_TOP, 0);
  OP1(SLJIT_MOV, SLJIT_SCRATCH_REG2, 0, ARGUMENTS, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_SCRATCH_REG2), SLJIT_OFFSETOF(jit_arguments, uchar_ptr), STR_PTR, 0);
  sljit_emit_ijump(compiler, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(do_utf_caselesscmp));
  OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
  if (common->mode == JIT_COMPILE)
    add_jump(compiler, backtracks, CMP(SLJIT_C_LESS_EQUAL, SLJIT_RETURN_REG, 0, SLJIT_IMM, 1));
  else
    {
    add_jump(compiler, backtracks, CMP(SLJIT_C_EQUAL, SLJIT_RETURN_REG, 0, SLJIT_IMM, 0));
    nopartial = CMP(SLJIT_C_NOT_EQUAL, SLJIT_RETURN_REG, 0, SLJIT_IMM, 1);
    check_partial(common, FALSE);
    add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));
    JUMPHERE(nopartial);
    }
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_RETURN_REG, 0);
  }
else
#endif /* SUPPORT_UTF && SUPPORT_UCP */
  {
  OP2(SLJIT_SUB | SLJIT_SET_E, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), TMP1, 0);
  if (withchecks)
    jump = JUMP(SLJIT_C_ZERO);

  OP2(SLJIT_ADD, STR_PTR, 0, STR_PTR, 0, TMP2, 0);
  partial = CMP(SLJIT_C_GREATER, STR_PTR, 0, STR_END, 0);
  if (common->mode == JIT_COMPILE)
    add_jump(compiler, backtracks, partial);

  add_jump(compiler, *cc == OP_REF ? &common->casefulcmp : &common->caselesscmp, JUMP(SLJIT_FAST_CALL));
  add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));

  if (common->mode != JIT_COMPILE)
    {
    nopartial = JUMP(SLJIT_JUMP);
    JUMPHERE(partial);
    /* TMP2 -= STR_END - STR_PTR */
    OP2(SLJIT_SUB, TMP2, 0, TMP2, 0, STR_PTR, 0);
    OP2(SLJIT_ADD, TMP2, 0, TMP2, 0, STR_END, 0);
    partial = CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, 0);
    OP1(SLJIT_MOV, STR_PTR, 0, STR_END, 0);
    add_jump(compiler, *cc == OP_REF ? &common->casefulcmp : &common->caselesscmp, JUMP(SLJIT_FAST_CALL));
    add_jump(compiler, backtracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));
    JUMPHERE(partial);
    check_partial(common, FALSE);
    add_jump(compiler, backtracks, JUMP(SLJIT_JUMP));
    JUMPHERE(nopartial);
    }
  }

if (jump != NULL)
  {
  if (emptyfail)
    add_jump(compiler, backtracks, jump);
  else
    JUMPHERE(jump);
  }
return cc + 1 + IMM2_SIZE;
}

static SLJIT_INLINE pcre_uchar *compile_ref_iterator_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;
pcre_uchar type;
struct sljit_label *label;
struct sljit_jump *zerolength;
struct sljit_jump *jump = NULL;
pcre_uchar *ccbegin = cc;
int min = 0, max = 0;
BOOL minimize;

PUSH_BACKTRACK(sizeof(iterator_backtrack), cc, NULL);

type = cc[1 + IMM2_SIZE];
minimize = (type & 0x1) != 0;
switch(type)
  {
  case OP_CRSTAR:
  case OP_CRMINSTAR:
  min = 0;
  max = 0;
  cc += 1 + IMM2_SIZE + 1;
  break;
  case OP_CRPLUS:
  case OP_CRMINPLUS:
  min = 1;
  max = 0;
  cc += 1 + IMM2_SIZE + 1;
  break;
  case OP_CRQUERY:
  case OP_CRMINQUERY:
  min = 0;
  max = 1;
  cc += 1 + IMM2_SIZE + 1;
  break;
  case OP_CRRANGE:
  case OP_CRMINRANGE:
  min = GET2(cc, 1 + IMM2_SIZE + 1);
  max = GET2(cc, 1 + IMM2_SIZE + 1 + IMM2_SIZE);
  cc += 1 + IMM2_SIZE + 1 + 2 * IMM2_SIZE;
  break;
  default:
  SLJIT_ASSERT_STOP();
  break;
  }

if (!minimize)
  {
  if (min == 0)
    {
    allocate_stack(common, 2);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_IMM, 0);
    /* Temporary release of STR_PTR. */
    OP2(SLJIT_SUB, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
    zerolength = compile_ref_checks(common, ccbegin, NULL);
    /* Restore if not zero length. */
    OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
    }
  else
    {
    allocate_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
    zerolength = compile_ref_checks(common, ccbegin, &backtrack->topbacktracks);
    }

  if (min > 1 || max > 1)
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0, SLJIT_IMM, 0);

  label = LABEL();
  compile_ref_matchingpath(common, ccbegin, &backtrack->topbacktracks, FALSE, FALSE);

  if (min > 1 || max > 1)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0);
    OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0, TMP1, 0);
    if (min > 1)
      CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, min, label);
    if (max > 1)
      {
      jump = CMP(SLJIT_C_GREATER_EQUAL, TMP1, 0, SLJIT_IMM, max);
      allocate_stack(common, 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      JUMPTO(SLJIT_JUMP, label);
      JUMPHERE(jump);
      }
    }

  if (max == 0)
    {
    /* Includes min > 1 case as well. */
    allocate_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
    JUMPTO(SLJIT_JUMP, label);
    }

  JUMPHERE(zerolength);
  BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();

  decrease_call_count(common);
  return cc;
  }

allocate_stack(common, 2);
OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
if (type != OP_CRMINSTAR)
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_IMM, 0);

if (min == 0)
  {
  zerolength = compile_ref_checks(common, ccbegin, NULL);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
  jump = JUMP(SLJIT_JUMP);
  }
else
  zerolength = compile_ref_checks(common, ccbegin, &backtrack->topbacktracks);

BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();
if (max > 0)
  add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_GREATER_EQUAL, SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_IMM, max));

compile_ref_matchingpath(common, ccbegin, &backtrack->topbacktracks, TRUE, TRUE);
OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);

if (min > 1)
  {
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
  OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP1, 0);
  CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, min, BACKTRACK_AS(iterator_backtrack)->matchingpath);
  }
else if (max > 0)
  OP2(SLJIT_ADD, SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_IMM, 1);

if (jump != NULL)
  JUMPHERE(jump);
JUMPHERE(zerolength);

decrease_call_count(common);
return cc;
}

static SLJIT_INLINE pcre_uchar *compile_recurse_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;
recurse_entry *entry = common->entries;
recurse_entry *prev = NULL;
int start = GET(cc, 1);

PUSH_BACKTRACK(sizeof(recurse_backtrack), cc, NULL);
while (entry != NULL)
  {
  if (entry->start == start)
    break;
  prev = entry;
  entry = entry->next;
  }

if (entry == NULL)
  {
  entry = sljit_alloc_memory(compiler, sizeof(recurse_entry));
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    return NULL;
  entry->next = NULL;
  entry->entry = NULL;
  entry->calls = NULL;
  entry->start = start;

  if (prev != NULL)
    prev->next = entry;
  else
    common->entries = entry;
  }

if (common->has_set_som && common->mark_ptr != 0)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));
  allocate_stack(common, 2);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP2, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP1, 0);
  }
else if (common->has_set_som || common->mark_ptr != 0)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->has_set_som ? (int)(OVECTOR(0)) : common->mark_ptr);
  allocate_stack(common, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP2, 0);
  }

if (entry->entry == NULL)
  add_jump(compiler, &entry->calls, JUMP(SLJIT_FAST_CALL));
else
  JUMPTO(SLJIT_FAST_CALL, entry->entry);
/* Leave if the match is failed. */
add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, 0));
return cc + 1 + LINK_SIZE;
}

static pcre_uchar *compile_assert_matchingpath(compiler_common *common, pcre_uchar *cc, assert_backtrack *backtrack, BOOL conditional)
{
DEFINE_COMPILER;
int framesize;
int private_data_ptr;
backtrack_common altbacktrack;
pcre_uchar *ccbegin;
pcre_uchar opcode;
pcre_uchar bra = OP_BRA;
jump_list *tmp = NULL;
jump_list **target = (conditional) ? &backtrack->condfailed : &backtrack->common.topbacktracks;
jump_list **found;
/* Saving previous accept variables. */
struct sljit_label *save_quitlabel = common->quitlabel;
struct sljit_label *save_acceptlabel = common->acceptlabel;
jump_list *save_quit = common->quit;
jump_list *save_accept = common->accept;
struct sljit_jump *jump;
struct sljit_jump *brajump = NULL;

if (*cc == OP_BRAZERO || *cc == OP_BRAMINZERO)
  {
  SLJIT_ASSERT(!conditional);
  bra = *cc;
  cc++;
  }
private_data_ptr = PRIVATE_DATA(cc);
SLJIT_ASSERT(private_data_ptr != 0);
framesize = get_framesize(common, cc, FALSE);
backtrack->framesize = framesize;
backtrack->private_data_ptr = private_data_ptr;
opcode = *cc;
SLJIT_ASSERT(opcode >= OP_ASSERT && opcode <= OP_ASSERTBACK_NOT);
found = (opcode == OP_ASSERT || opcode == OP_ASSERTBACK) ? &tmp : target;
ccbegin = cc;
cc += GET(cc, 1);

if (bra == OP_BRAMINZERO)
  {
  /* This is a braminzero backtrack path. */
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  brajump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
  }

if (framesize < 0)
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STACK_TOP, 0);
  allocate_stack(common, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
  }
else
  {
  allocate_stack(common, framesize + 2);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
  OP2(SLJIT_SUB, TMP2, 0, STACK_TOP, 0, SLJIT_IMM, -STACK(framesize + 1));
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP2, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP1, 0);
  init_frame(common, ccbegin, framesize + 1, 2, FALSE);
  }

memset(&altbacktrack, 0, sizeof(backtrack_common));
common->quitlabel = NULL;
common->quit = NULL;
while (1)
  {
  common->acceptlabel = NULL;
  common->accept = NULL;
  altbacktrack.top = NULL;
  altbacktrack.topbacktracks = NULL;

  if (*ccbegin == OP_ALT)
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));

  altbacktrack.cc = ccbegin;
  compile_matchingpath(common, ccbegin + 1 + LINK_SIZE, cc, &altbacktrack);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    {
    common->quitlabel = save_quitlabel;
    common->acceptlabel = save_acceptlabel;
    common->quit = save_quit;
    common->accept = save_accept;
    return NULL;
    }
  common->acceptlabel = LABEL();
  if (common->accept != NULL)
    set_jumps(common->accept, common->acceptlabel);

  /* Reset stack. */
  if (framesize < 0)
    OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
  else {
    if ((opcode != OP_ASSERT_NOT && opcode != OP_ASSERTBACK_NOT) || conditional)
      {
      /* We don't need to keep the STR_PTR, only the previous private_data_ptr. */
      OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, (framesize + 1) * sizeof(sljit_sw));
      }
    else
      {
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
      }
  }

  if (opcode == OP_ASSERT_NOT || opcode == OP_ASSERTBACK_NOT)
    {
    /* We know that STR_PTR was stored on the top of the stack. */
    if (conditional)
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), 0);
    else if (bra == OP_BRAZERO)
      {
      if (framesize < 0)
        OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), 0);
      else
        {
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), framesize * sizeof(sljit_sw));
        OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), (framesize + 1) * sizeof(sljit_sw));
        OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP1, 0);
        }
      OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }
    else if (framesize >= 0)
      {
      /* For OP_BRA and OP_BRAMINZERO. */
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), framesize * sizeof(sljit_sw));
      }
    }
  add_jump(compiler, found, JUMP(SLJIT_JUMP));

  compile_backtrackingpath(common, altbacktrack.top);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    {
    common->quitlabel = save_quitlabel;
    common->acceptlabel = save_acceptlabel;
    common->quit = save_quit;
    common->accept = save_accept;
    return NULL;
    }
  set_jumps(altbacktrack.topbacktracks, LABEL());

  if (*cc != OP_ALT)
    break;

  ccbegin = cc;
  cc += GET(cc, 1);
  }
/* None of them matched. */
if (common->quit != NULL)
  set_jumps(common->quit, LABEL());

if (opcode == OP_ASSERT || opcode == OP_ASSERTBACK)
  {
  /* Assert is failed. */
  if (conditional || bra == OP_BRAZERO)
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));

  if (framesize < 0)
    {
    /* The topmost item should be 0. */
    if (bra == OP_BRAZERO)
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
    else
      free_stack(common, 1);
    }
  else
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    /* The topmost item should be 0. */
    if (bra == OP_BRAZERO)
      {
      free_stack(common, framesize + 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }
    else
      free_stack(common, framesize + 2);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP1, 0);
    }
  jump = JUMP(SLJIT_JUMP);
  if (bra != OP_BRAZERO)
    add_jump(compiler, target, jump);

  /* Assert is successful. */
  set_jumps(tmp, LABEL());
  if (framesize < 0)
    {
    /* We know that STR_PTR was stored on the top of the stack. */
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), 0);
    /* Keep the STR_PTR on the top of the stack. */
    if (bra == OP_BRAZERO)
      OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
    else if (bra == OP_BRAMINZERO)
      {
      OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }
    }
  else
    {
    if (bra == OP_BRA)
      {
      /* We don't need to keep the STR_PTR, only the previous private_data_ptr. */
      OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, (framesize + 1) * sizeof(sljit_sw));
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), 0);
      }
    else
      {
      /* We don't need to keep the STR_PTR, only the previous private_data_ptr. */
      OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, (framesize + 2) * sizeof(sljit_sw));
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), bra == OP_BRAZERO ? STR_PTR : SLJIT_IMM, 0);
      }
    }

  if (bra == OP_BRAZERO)
    {
    backtrack->matchingpath = LABEL();
    sljit_set_label(jump, backtrack->matchingpath);
    }
  else if (bra == OP_BRAMINZERO)
    {
    JUMPTO(SLJIT_JUMP, backtrack->matchingpath);
    JUMPHERE(brajump);
    if (framesize >= 0)
      {
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), framesize * sizeof(sljit_sw));
      }
    set_jumps(backtrack->common.topbacktracks, LABEL());
    }
  }
else
  {
  /* AssertNot is successful. */
  if (framesize < 0)
    {
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    if (bra != OP_BRA)
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
    else
      free_stack(common, 1);
    }
  else
    {
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    /* The topmost item should be 0. */
    if (bra != OP_BRA)
      {
      free_stack(common, framesize + 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }
    else
      free_stack(common, framesize + 2);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP1, 0);
    }

  if (bra == OP_BRAZERO)
    backtrack->matchingpath = LABEL();
  else if (bra == OP_BRAMINZERO)
    {
    JUMPTO(SLJIT_JUMP, backtrack->matchingpath);
    JUMPHERE(brajump);
    }

  if (bra != OP_BRA)
    {
    SLJIT_ASSERT(found == &backtrack->common.topbacktracks);
    set_jumps(backtrack->common.topbacktracks, LABEL());
    backtrack->common.topbacktracks = NULL;
    }
  }

common->quitlabel = save_quitlabel;
common->acceptlabel = save_acceptlabel;
common->quit = save_quit;
common->accept = save_accept;
return cc + 1 + LINK_SIZE;
}

static sljit_sw SLJIT_CALL do_searchovector(sljit_uw refno, sljit_sw* locals, pcre_uchar *name_table)
{
int condition = FALSE;
pcre_uchar *slotA = name_table;
pcre_uchar *slotB;
sljit_sw name_count = locals[LOCALS0 / sizeof(sljit_sw)];
sljit_sw name_entry_size = locals[LOCALS1 / sizeof(sljit_sw)];
sljit_sw no_capture;
int i;

locals += refno & 0xff;
refno >>= 8;
no_capture = locals[1];

for (i = 0; i < name_count; i++)
  {
  if (GET2(slotA, 0) == refno) break;
  slotA += name_entry_size;
  }

if (i < name_count)
  {
  /* Found a name for the number - there can be only one; duplicate names
  for different numbers are allowed, but not vice versa. First scan down
  for duplicates. */

  slotB = slotA;
  while (slotB > name_table)
    {
    slotB -= name_entry_size;
    if (STRCMP_UC_UC(slotA + IMM2_SIZE, slotB + IMM2_SIZE) == 0)
      {
      condition = locals[GET2(slotB, 0) << 1] != no_capture;
      if (condition) break;
      }
    else break;
    }

  /* Scan up for duplicates */
  if (!condition)
    {
    slotB = slotA;
    for (i++; i < name_count; i++)
      {
      slotB += name_entry_size;
      if (STRCMP_UC_UC(slotA + IMM2_SIZE, slotB + IMM2_SIZE) == 0)
        {
        condition = locals[GET2(slotB, 0) << 1] != no_capture;
        if (condition) break;
        }
      else break;
      }
    }
  }
return condition;
}

static sljit_sw SLJIT_CALL do_searchgroups(sljit_uw recno, sljit_uw* locals, pcre_uchar *name_table)
{
int condition = FALSE;
pcre_uchar *slotA = name_table;
pcre_uchar *slotB;
sljit_uw name_count = locals[LOCALS0 / sizeof(sljit_sw)];
sljit_uw name_entry_size = locals[LOCALS1 / sizeof(sljit_sw)];
sljit_uw group_num = locals[POSSESSIVE0 / sizeof(sljit_sw)];
sljit_uw i;

for (i = 0; i < name_count; i++)
  {
  if (GET2(slotA, 0) == recno) break;
  slotA += name_entry_size;
  }

if (i < name_count)
  {
  /* Found a name for the number - there can be only one; duplicate
  names for different numbers are allowed, but not vice versa. First
  scan down for duplicates. */

  slotB = slotA;
  while (slotB > name_table)
    {
    slotB -= name_entry_size;
    if (STRCMP_UC_UC(slotA + IMM2_SIZE, slotB + IMM2_SIZE) == 0)
      {
      condition = GET2(slotB, 0) == group_num;
      if (condition) break;
      }
    else break;
    }

  /* Scan up for duplicates */
  if (!condition)
    {
    slotB = slotA;
    for (i++; i < name_count; i++)
      {
      slotB += name_entry_size;
      if (STRCMP_UC_UC(slotA + IMM2_SIZE, slotB + IMM2_SIZE) == 0)
        {
        condition = GET2(slotB, 0) == group_num;
        if (condition) break;
        }
      else break;
      }
    }
  }
return condition;
}

/*
  Handling bracketed expressions is probably the most complex part.

  Stack layout naming characters:
    S - Push the current STR_PTR
    0 - Push a 0 (NULL)
    A - Push the current STR_PTR. Needed for restoring the STR_PTR
        before the next alternative. Not pushed if there are no alternatives.
    M - Any values pushed by the current alternative. Can be empty, or anything.
    C - Push the previous OVECTOR(i), OVECTOR(i+1) and OVECTOR_PRIV(i) to the stack.
    L - Push the previous local (pointed by localptr) to the stack
   () - opional values stored on the stack
  ()* - optonal, can be stored multiple times

  The following list shows the regular expression templates, their PCRE byte codes
  and stack layout supported by pcre-sljit.

  (?:)                     OP_BRA     | OP_KET                A M
  ()                       OP_CBRA    | OP_KET                C M
  (?:)+                    OP_BRA     | OP_KETRMAX        0   A M S   ( A M S )*
                           OP_SBRA    | OP_KETRMAX        0   L M S   ( L M S )*
  (?:)+?                   OP_BRA     | OP_KETRMIN        0   A M S   ( A M S )*
                           OP_SBRA    | OP_KETRMIN        0   L M S   ( L M S )*
  ()+                      OP_CBRA    | OP_KETRMAX        0   C M S   ( C M S )*
                           OP_SCBRA   | OP_KETRMAX        0   C M S   ( C M S )*
  ()+?                     OP_CBRA    | OP_KETRMIN        0   C M S   ( C M S )*
                           OP_SCBRA   | OP_KETRMIN        0   C M S   ( C M S )*
  (?:)?    OP_BRAZERO    | OP_BRA     | OP_KET            S ( A M 0 )
  (?:)??   OP_BRAMINZERO | OP_BRA     | OP_KET            S ( A M 0 )
  ()?      OP_BRAZERO    | OP_CBRA    | OP_KET            S ( C M 0 )
  ()??     OP_BRAMINZERO | OP_CBRA    | OP_KET            S ( C M 0 )
  (?:)*    OP_BRAZERO    | OP_BRA     | OP_KETRMAX      S 0 ( A M S )*
           OP_BRAZERO    | OP_SBRA    | OP_KETRMAX      S 0 ( L M S )*
  (?:)*?   OP_BRAMINZERO | OP_BRA     | OP_KETRMIN      S 0 ( A M S )*
           OP_BRAMINZERO | OP_SBRA    | OP_KETRMIN      S 0 ( L M S )*
  ()*      OP_BRAZERO    | OP_CBRA    | OP_KETRMAX      S 0 ( C M S )*
           OP_BRAZERO    | OP_SCBRA   | OP_KETRMAX      S 0 ( C M S )*
  ()*?     OP_BRAMINZERO | OP_CBRA    | OP_KETRMIN      S 0 ( C M S )*
           OP_BRAMINZERO | OP_SCBRA   | OP_KETRMIN      S 0 ( C M S )*


  Stack layout naming characters:
    A - Push the alternative index (starting from 0) on the stack.
        Not pushed if there is no alternatives.
    M - Any values pushed by the current alternative. Can be empty, or anything.

  The next list shows the possible content of a bracket:
  (|)     OP_*BRA    | OP_ALT ...         M A
  (?()|)  OP_*COND   | OP_ALT             M A
  (?>|)   OP_ONCE    | OP_ALT ...         [stack trace] M A
  (?>|)   OP_ONCE_NC | OP_ALT ...         [stack trace] M A
                                          Or nothing, if trace is unnecessary
*/

static pcre_uchar *compile_bracket_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;
pcre_uchar opcode;
int private_data_ptr = 0;
int offset = 0;
int stacksize;
pcre_uchar *ccbegin;
pcre_uchar *matchingpath;
pcre_uchar bra = OP_BRA;
pcre_uchar ket;
assert_backtrack *assert;
BOOL has_alternatives;
struct sljit_jump *jump;
struct sljit_jump *skip;
struct sljit_label *rmaxlabel = NULL;
struct sljit_jump *braminzerojump = NULL;

PUSH_BACKTRACK(sizeof(bracket_backtrack), cc, NULL);

if (*cc == OP_BRAZERO || *cc == OP_BRAMINZERO)
  {
  bra = *cc;
  cc++;
  opcode = *cc;
  }

opcode = *cc;
ccbegin = cc;
matchingpath = ccbegin + 1 + LINK_SIZE;

if ((opcode == OP_COND || opcode == OP_SCOND) && cc[1 + LINK_SIZE] == OP_DEF)
  {
  /* Drop this bracket_backtrack. */
  parent->top = backtrack->prev;
  return bracketend(cc);
  }

ket = *(bracketend(cc) - 1 - LINK_SIZE);
SLJIT_ASSERT(ket == OP_KET || ket == OP_KETRMAX || ket == OP_KETRMIN);
SLJIT_ASSERT(!((bra == OP_BRAZERO && ket == OP_KETRMIN) || (bra == OP_BRAMINZERO && ket == OP_KETRMAX)));
cc += GET(cc, 1);

has_alternatives = *cc == OP_ALT;
if (SLJIT_UNLIKELY(opcode == OP_COND) || SLJIT_UNLIKELY(opcode == OP_SCOND))
  {
  has_alternatives = (*matchingpath == OP_RREF) ? FALSE : TRUE;
  if (*matchingpath == OP_NRREF)
    {
    stacksize = GET2(matchingpath, 1);
    if (common->currententry == NULL || stacksize == RREF_ANY)
      has_alternatives = FALSE;
    else if (common->currententry->start == 0)
      has_alternatives = stacksize != 0;
    else
      has_alternatives = stacksize != (int)GET2(common->start, common->currententry->start + 1 + LINK_SIZE);
    }
  }

if (SLJIT_UNLIKELY(opcode == OP_COND) && (*cc == OP_KETRMAX || *cc == OP_KETRMIN))
  opcode = OP_SCOND;
if (SLJIT_UNLIKELY(opcode == OP_ONCE_NC))
  opcode = OP_ONCE;

if (opcode == OP_CBRA || opcode == OP_SCBRA)
  {
  /* Capturing brackets has a pre-allocated space. */
  offset = GET2(ccbegin, 1 + LINK_SIZE);
  if (common->optimized_cbracket[offset] == 0)
    {
    private_data_ptr = OVECTOR_PRIV(offset);
    offset <<= 1;
    }
  else
    {
    offset <<= 1;
    private_data_ptr = OVECTOR(offset);
    }
  BACKTRACK_AS(bracket_backtrack)->private_data_ptr = private_data_ptr;
  matchingpath += IMM2_SIZE;
  }
else if (opcode == OP_ONCE || opcode == OP_SBRA || opcode == OP_SCOND)
  {
  /* Other brackets simply allocate the next entry. */
  private_data_ptr = PRIVATE_DATA(ccbegin);
  SLJIT_ASSERT(private_data_ptr != 0);
  BACKTRACK_AS(bracket_backtrack)->private_data_ptr = private_data_ptr;
  if (opcode == OP_ONCE)
    BACKTRACK_AS(bracket_backtrack)->u.framesize = get_framesize(common, ccbegin, FALSE);
  }

/* Instructions before the first alternative. */
stacksize = 0;
if ((ket == OP_KETRMAX) || (ket == OP_KETRMIN && bra != OP_BRAMINZERO))
  stacksize++;
if (bra == OP_BRAZERO)
  stacksize++;

if (stacksize > 0)
  allocate_stack(common, stacksize);

stacksize = 0;
if ((ket == OP_KETRMAX) || (ket == OP_KETRMIN && bra != OP_BRAMINZERO))
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), SLJIT_IMM, 0);
  stacksize++;
  }

if (bra == OP_BRAZERO)
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), STR_PTR, 0);

if (bra == OP_BRAMINZERO)
  {
  /* This is a backtrack path! (Since the try-path of OP_BRAMINZERO matches to the empty string) */
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  if (ket != OP_KETRMIN)
    {
    free_stack(common, 1);
    braminzerojump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
    }
  else
    {
    if (opcode == OP_ONCE || opcode >= OP_SBRA)
      {
      jump = CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
      /* Nothing stored during the first run. */
      skip = JUMP(SLJIT_JUMP);
      JUMPHERE(jump);
      /* Checking zero-length iteration. */
      if (opcode != OP_ONCE || BACKTRACK_AS(bracket_backtrack)->u.framesize < 0)
        {
        /* When we come from outside, private_data_ptr contains the previous STR_PTR. */
        braminzerojump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
        }
      else
        {
        /* Except when the whole stack frame must be saved. */
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
        braminzerojump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_MEM1(TMP1), (BACKTRACK_AS(bracket_backtrack)->u.framesize + 1) * sizeof(sljit_sw));
        }
      JUMPHERE(skip);
      }
    else
      {
      jump = CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
      JUMPHERE(jump);
      }
    }
  }

if (ket == OP_KETRMIN)
  BACKTRACK_AS(bracket_backtrack)->recursive_matchingpath = LABEL();

if (ket == OP_KETRMAX)
  {
  rmaxlabel = LABEL();
  if (has_alternatives && opcode != OP_ONCE && opcode < OP_SBRA)
    BACKTRACK_AS(bracket_backtrack)->alternative_matchingpath = rmaxlabel;
  }

/* Handling capturing brackets and alternatives. */
if (opcode == OP_ONCE)
  {
  if (BACKTRACK_AS(bracket_backtrack)->u.framesize < 0)
    {
    /* Neither capturing brackets nor recursions are not found in the block. */
    if (ket == OP_KETRMIN)
      {
      OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      allocate_stack(common, 2);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP2, 0);
      OP2(SLJIT_SUB, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
      }
    else if (ket == OP_KETRMAX || has_alternatives)
      {
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STACK_TOP, 0);
      allocate_stack(common, 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      }
    else
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STACK_TOP, 0);
    }
  else
    {
    if (ket == OP_KETRMIN || ket == OP_KETRMAX || has_alternatives)
      {
      allocate_stack(common, BACKTRACK_AS(bracket_backtrack)->u.framesize + 2);
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP2(SLJIT_SUB, TMP2, 0, STACK_TOP, 0, SLJIT_IMM, -STACK(BACKTRACK_AS(bracket_backtrack)->u.framesize + 1));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP2, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP1, 0);
      init_frame(common, ccbegin, BACKTRACK_AS(bracket_backtrack)->u.framesize + 1, 2, FALSE);
      }
    else
      {
      allocate_stack(common, BACKTRACK_AS(bracket_backtrack)->u.framesize + 1);
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP2(SLJIT_SUB, TMP2, 0, STACK_TOP, 0, SLJIT_IMM, -STACK(BACKTRACK_AS(bracket_backtrack)->u.framesize));
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP2, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP1, 0);
      init_frame(common, ccbegin, BACKTRACK_AS(bracket_backtrack)->u.framesize, 1, FALSE);
      }
    }
  }
else if (opcode == OP_CBRA || opcode == OP_SCBRA)
  {
  /* Saving the previous values. */
  if (common->optimized_cbracket[offset >> 1] == 0)
    {
    allocate_stack(common, 3);
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP1, 0);
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP2, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STR_PTR, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(2), TMP1, 0);
    }
  else
    {
    SLJIT_ASSERT(private_data_ptr == OVECTOR(offset));
    allocate_stack(common, 2);
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr + sizeof(sljit_sw));
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STR_PTR, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP1, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP2, 0);
    }
  }
else if (opcode == OP_SBRA || opcode == OP_SCOND)
  {
  /* Saving the previous value. */
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
  allocate_stack(common, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP2, 0);
  }
else if (has_alternatives)
  {
  /* Pushing the starting string pointer. */
  allocate_stack(common, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
  }

/* Generating code for the first alternative. */
if (opcode == OP_COND || opcode == OP_SCOND)
  {
  if (*matchingpath == OP_CREF)
    {
    SLJIT_ASSERT(has_alternatives);
    add_jump(compiler, &(BACKTRACK_AS(bracket_backtrack)->u.condfailed),
      CMP(SLJIT_C_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(GET2(matchingpath, 1) << 1), SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1)));
    matchingpath += 1 + IMM2_SIZE;
    }
  else if (*matchingpath == OP_NCREF)
    {
    SLJIT_ASSERT(has_alternatives);
    stacksize = GET2(matchingpath, 1);
    jump = CMP(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(stacksize << 1), SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(1));

    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1, STACK_TOP, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, SLJIT_IMM, common->name_count);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, SLJIT_IMM, common->name_entry_size);
    OP1(SLJIT_MOV, SLJIT_SCRATCH_REG1, 0, SLJIT_IMM, (stacksize << 8) | (common->ovector_start / sizeof(sljit_sw)));
    GET_LOCAL_BASE(SLJIT_SCRATCH_REG2, 0, 0);
    OP1(SLJIT_MOV, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, common->name_table);
    sljit_emit_ijump(compiler, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(do_searchovector));
    OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1);
    add_jump(compiler, &(BACKTRACK_AS(bracket_backtrack)->u.condfailed), CMP(SLJIT_C_EQUAL, SLJIT_SCRATCH_REG1, 0, SLJIT_IMM, 0));

    JUMPHERE(jump);
    matchingpath += 1 + IMM2_SIZE;
    }
  else if (*matchingpath == OP_RREF || *matchingpath == OP_NRREF)
    {
    /* Never has other case. */
    BACKTRACK_AS(bracket_backtrack)->u.condfailed = NULL;

    stacksize = GET2(matchingpath, 1);
    if (common->currententry == NULL)
      stacksize = 0;
    else if (stacksize == RREF_ANY)
      stacksize = 1;
    else if (common->currententry->start == 0)
      stacksize = stacksize == 0;
    else
      stacksize = stacksize == (int)GET2(common->start, common->currententry->start + 1 + LINK_SIZE);

    if (*matchingpath == OP_RREF || stacksize || common->currententry == NULL)
      {
      SLJIT_ASSERT(!has_alternatives);
      if (stacksize != 0)
        matchingpath += 1 + IMM2_SIZE;
      else
        {
        if (*cc == OP_ALT)
          {
          matchingpath = cc + 1 + LINK_SIZE;
          cc += GET(cc, 1);
          }
        else
          matchingpath = cc;
        }
      }
    else
      {
      SLJIT_ASSERT(has_alternatives);

      stacksize = GET2(matchingpath, 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1, STACK_TOP, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0, SLJIT_IMM, common->name_count);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, SLJIT_IMM, common->name_entry_size);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0, SLJIT_IMM, GET2(common->start, common->currententry->start + 1 + LINK_SIZE));
      OP1(SLJIT_MOV, SLJIT_SCRATCH_REG1, 0, SLJIT_IMM, stacksize);
      GET_LOCAL_BASE(SLJIT_SCRATCH_REG2, 0, 0);
      OP1(SLJIT_MOV, SLJIT_SCRATCH_REG3, 0, SLJIT_IMM, common->name_table);
      sljit_emit_ijump(compiler, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(do_searchgroups));
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1);
      add_jump(compiler, &(BACKTRACK_AS(bracket_backtrack)->u.condfailed), CMP(SLJIT_C_EQUAL, SLJIT_SCRATCH_REG1, 0, SLJIT_IMM, 0));
      matchingpath += 1 + IMM2_SIZE;
      }
    }
  else
    {
    SLJIT_ASSERT(has_alternatives && *matchingpath >= OP_ASSERT && *matchingpath <= OP_ASSERTBACK_NOT);
    /* Similar code as PUSH_BACKTRACK macro. */
    assert = sljit_alloc_memory(compiler, sizeof(assert_backtrack));
    if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
      return NULL;
    memset(assert, 0, sizeof(assert_backtrack));
    assert->common.cc = matchingpath;
    BACKTRACK_AS(bracket_backtrack)->u.assert = assert;
    matchingpath = compile_assert_matchingpath(common, matchingpath, assert, TRUE);
    }
  }

compile_matchingpath(common, matchingpath, cc, backtrack);
if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
  return NULL;

if (opcode == OP_ONCE)
  {
  if (BACKTRACK_AS(bracket_backtrack)->u.framesize < 0)
    {
    OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
    /* TMP2 which is set here used by OP_KETRMAX below. */
    if (ket == OP_KETRMAX)
      OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), 0);
    else if (ket == OP_KETRMIN)
      {
      /* Move the STR_PTR to the private_data_ptr. */
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), 0);
      }
    }
  else
    {
    stacksize = (ket == OP_KETRMIN || ket == OP_KETRMAX || has_alternatives) ? 2 : 1;
    OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, (BACKTRACK_AS(bracket_backtrack)->u.framesize + stacksize) * sizeof(sljit_sw));
    if (ket == OP_KETRMAX)
      {
      /* TMP2 which is set here used by OP_KETRMAX below. */
      OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
      }
    }
  }

stacksize = 0;
if (ket != OP_KET || bra != OP_BRA)
  stacksize++;
if (has_alternatives && opcode != OP_ONCE)
  stacksize++;

if (stacksize > 0)
  allocate_stack(common, stacksize);

stacksize = 0;
if (ket != OP_KET)
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), STR_PTR, 0);
  stacksize++;
  }
else if (bra != OP_BRA)
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), SLJIT_IMM, 0);
  stacksize++;
  }

if (has_alternatives)
  {
  if (opcode != OP_ONCE)
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), SLJIT_IMM, 0);
  if (ket != OP_KETRMAX)
    BACKTRACK_AS(bracket_backtrack)->alternative_matchingpath = LABEL();
  }

/* Must be after the matchingpath label. */
if (offset != 0)
  {
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), STR_PTR, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 0), TMP1, 0);
  }

if (ket == OP_KETRMAX)
  {
  if (opcode == OP_ONCE || opcode >= OP_SBRA)
    {
    if (has_alternatives)
      BACKTRACK_AS(bracket_backtrack)->alternative_matchingpath = LABEL();
    /* Checking zero-length iteration. */
    if (opcode != OP_ONCE)
      {
      CMPTO(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STR_PTR, 0, rmaxlabel);
      /* Drop STR_PTR for greedy plus quantifier. */
      if (bra != OP_BRAZERO)
        free_stack(common, 1);
      }
    else
      /* TMP2 must contain the starting STR_PTR. */
      CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_PTR, 0, rmaxlabel);
    }
  else
    JUMPTO(SLJIT_JUMP, rmaxlabel);
  BACKTRACK_AS(bracket_backtrack)->recursive_matchingpath = LABEL();
  }

if (bra == OP_BRAZERO)
  BACKTRACK_AS(bracket_backtrack)->zero_matchingpath = LABEL();

if (bra == OP_BRAMINZERO)
  {
  /* This is a backtrack path! (From the viewpoint of OP_BRAMINZERO) */
  JUMPTO(SLJIT_JUMP, ((braminzero_backtrack *)parent)->matchingpath);
  if (braminzerojump != NULL)
    {
    JUMPHERE(braminzerojump);
    /* We need to release the end pointer to perform the
    backtrack for the zero-length iteration. When
    framesize is < 0, OP_ONCE will do the release itself. */
    if (opcode == OP_ONCE && BACKTRACK_AS(bracket_backtrack)->u.framesize >= 0)
      {
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
      }
    else if (ket == OP_KETRMIN && opcode != OP_ONCE)
      free_stack(common, 1);
    }
  /* Continue to the normal backtrack. */
  }

if ((ket != OP_KET && bra != OP_BRAMINZERO) || bra == OP_BRAZERO)
  decrease_call_count(common);

/* Skip the other alternatives. */
while (*cc == OP_ALT)
  cc += GET(cc, 1);
cc += 1 + LINK_SIZE;
return cc;
}

static pcre_uchar *compile_bracketpos_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;
pcre_uchar opcode;
int private_data_ptr;
int cbraprivptr = 0;
int framesize;
int stacksize;
int offset = 0;
BOOL zero = FALSE;
pcre_uchar *ccbegin = NULL;
int stack;
struct sljit_label *loop = NULL;
struct jump_list *emptymatch = NULL;

PUSH_BACKTRACK(sizeof(bracketpos_backtrack), cc, NULL);
if (*cc == OP_BRAPOSZERO)
  {
  zero = TRUE;
  cc++;
  }

opcode = *cc;
private_data_ptr = PRIVATE_DATA(cc);
SLJIT_ASSERT(private_data_ptr != 0);
BACKTRACK_AS(bracketpos_backtrack)->private_data_ptr = private_data_ptr;
switch(opcode)
  {
  case OP_BRAPOS:
  case OP_SBRAPOS:
  ccbegin = cc + 1 + LINK_SIZE;
  break;

  case OP_CBRAPOS:
  case OP_SCBRAPOS:
  offset = GET2(cc, 1 + LINK_SIZE);
  /* This case cannot be optimized in the same was as
  normal capturing brackets. */
  SLJIT_ASSERT(common->optimized_cbracket[offset] == 0);
  cbraprivptr = OVECTOR_PRIV(offset);
  offset <<= 1;
  ccbegin = cc + 1 + LINK_SIZE + IMM2_SIZE;
  break;

  default:
  SLJIT_ASSERT_STOP();
  break;
  }

framesize = get_framesize(common, cc, FALSE);
BACKTRACK_AS(bracketpos_backtrack)->framesize = framesize;
if (framesize < 0)
  {
  stacksize = (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS) ? 2 : 1;
  if (!zero)
    stacksize++;
  BACKTRACK_AS(bracketpos_backtrack)->stacksize = stacksize;
  allocate_stack(common, stacksize);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, STACK_TOP, 0);

  if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1));
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP1, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), TMP2, 0);
    }
  else
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);

  if (!zero)
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize - 1), SLJIT_IMM, 1);
  }
else
  {
  stacksize = framesize + 1;
  if (!zero)
    stacksize++;
  if (opcode == OP_BRAPOS || opcode == OP_SBRAPOS)
    stacksize++;
  BACKTRACK_AS(bracketpos_backtrack)->stacksize = stacksize;
  allocate_stack(common, stacksize);

  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
  OP2(SLJIT_SUB, TMP2, 0, STACK_TOP, 0, SLJIT_IMM, -STACK(stacksize - 1));
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP2, 0);
  stack = 0;
  if (!zero)
    {
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 1);
    stack++;
    }
  if (opcode == OP_BRAPOS || opcode == OP_SBRAPOS)
    {
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stack), STR_PTR, 0);
    stack++;
    }
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stack), TMP1, 0);
  init_frame(common, cc, stacksize - 1, stacksize - framesize, FALSE);
  }

if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr, STR_PTR, 0);

loop = LABEL();
while (*cc != OP_KETRPOS)
  {
  backtrack->top = NULL;
  backtrack->topbacktracks = NULL;
  cc += GET(cc, 1);

  compile_matchingpath(common, ccbegin, cc, backtrack);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    return NULL;

  if (framesize < 0)
    {
    OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);

    if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr, STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
      }
    else
      {
      if (opcode == OP_SBRAPOS)
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      }

    if (opcode == OP_SBRAPOS || opcode == OP_SCBRAPOS)
      add_jump(compiler, &emptymatch, CMP(SLJIT_C_EQUAL, TMP1, 0, STR_PTR, 0));

    if (!zero)
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize - 1), SLJIT_IMM, 0);
    }
  else
    {
    if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
      {
      OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, stacksize * sizeof(sljit_sw));
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr, STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
      }
    else
      {
      OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP2(SLJIT_ADD, STACK_TOP, 0, TMP2, 0, SLJIT_IMM, stacksize * sizeof(sljit_sw));
      if (opcode == OP_SBRAPOS)
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP2), (framesize + 1) * sizeof(sljit_sw));
      OP1(SLJIT_MOV, SLJIT_MEM1(TMP2), (framesize + 1) * sizeof(sljit_sw), STR_PTR, 0);
      }

    if (opcode == OP_SBRAPOS || opcode == OP_SCBRAPOS)
      add_jump(compiler, &emptymatch, CMP(SLJIT_C_EQUAL, TMP1, 0, STR_PTR, 0));

    if (!zero)
      {
      if (framesize < 0)
        OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize - 1), SLJIT_IMM, 0);
      else
        OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }
    }
  JUMPTO(SLJIT_JUMP, loop);
  flush_stubs(common);

  compile_backtrackingpath(common, backtrack->top);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    return NULL;
  set_jumps(backtrack->topbacktracks, LABEL());

  if (framesize < 0)
    {
    if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr);
    else
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    }
  else
    {
    if (opcode == OP_CBRAPOS || opcode == OP_SCBRAPOS)
      {
      /* Last alternative. */
      if (*cc == OP_KETRPOS)
        OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), cbraprivptr);
      }
    else
      {
      OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(TMP2), (framesize + 1) * sizeof(sljit_sw));
      }
    }

  if (*cc == OP_KETRPOS)
    break;
  ccbegin = cc + 1 + LINK_SIZE;
  }

backtrack->topbacktracks = NULL;
if (!zero)
  {
  if (framesize < 0)
    add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(STACK_TOP), STACK(stacksize - 1), SLJIT_IMM, 0));
  else /* TMP2 is set to [private_data_ptr] above. */
    add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(TMP2), (stacksize - 1) * sizeof(sljit_sw), SLJIT_IMM, 0));
  }

/* None of them matched. */
set_jumps(emptymatch, LABEL());
decrease_call_count(common);
return cc + 1 + LINK_SIZE;
}

static SLJIT_INLINE pcre_uchar *get_iterator_parameters(compiler_common *common, pcre_uchar *cc, pcre_uchar *opcode, pcre_uchar *type, int *arg1, int *arg2, pcre_uchar **end)
{
int class_len;

*opcode = *cc;
if (*opcode >= OP_STAR && *opcode <= OP_POSUPTO)
  {
  cc++;
  *type = OP_CHAR;
  }
else if (*opcode >= OP_STARI && *opcode <= OP_POSUPTOI)
  {
  cc++;
  *type = OP_CHARI;
  *opcode -= OP_STARI - OP_STAR;
  }
else if (*opcode >= OP_NOTSTAR && *opcode <= OP_NOTPOSUPTO)
  {
  cc++;
  *type = OP_NOT;
  *opcode -= OP_NOTSTAR - OP_STAR;
  }
else if (*opcode >= OP_NOTSTARI && *opcode <= OP_NOTPOSUPTOI)
  {
  cc++;
  *type = OP_NOTI;
  *opcode -= OP_NOTSTARI - OP_STAR;
  }
else if (*opcode >= OP_TYPESTAR && *opcode <= OP_TYPEPOSUPTO)
  {
  cc++;
  *opcode -= OP_TYPESTAR - OP_STAR;
  *type = 0;
  }
else
  {
  SLJIT_ASSERT(*opcode >= OP_CLASS || *opcode <= OP_XCLASS);
  *type = *opcode;
  cc++;
  class_len = (*type < OP_XCLASS) ? (int)(1 + (32 / sizeof(pcre_uchar))) : GET(cc, 0);
  *opcode = cc[class_len - 1];
  if (*opcode >= OP_CRSTAR && *opcode <= OP_CRMINQUERY)
    {
    *opcode -= OP_CRSTAR - OP_STAR;
    if (end != NULL)
      *end = cc + class_len;
    }
  else
    {
    SLJIT_ASSERT(*opcode == OP_CRRANGE || *opcode == OP_CRMINRANGE);
    *arg1 = GET2(cc, (class_len + IMM2_SIZE));
    *arg2 = GET2(cc, class_len);

    if (*arg2 == 0)
      {
      SLJIT_ASSERT(*arg1 != 0);
      *opcode = (*opcode == OP_CRRANGE) ? OP_UPTO : OP_MINUPTO;
      }
    if (*arg1 == *arg2)
      *opcode = OP_EXACT;

    if (end != NULL)
      *end = cc + class_len + 2 * IMM2_SIZE;
    }
  return cc;
  }

if (*opcode == OP_UPTO || *opcode == OP_MINUPTO || *opcode == OP_EXACT || *opcode == OP_POSUPTO)
  {
  *arg1 = GET2(cc, 0);
  cc += IMM2_SIZE;
  }

if (*type == 0)
  {
  *type = *cc;
  if (end != NULL)
    *end = next_opcode(common, cc);
  cc++;
  return cc;
  }

if (end != NULL)
  {
  *end = cc + 1;
#ifdef SUPPORT_UTF
  if (common->utf && HAS_EXTRALEN(*cc)) *end += GET_EXTRALEN(*cc);
#endif
  }
return cc;
}

static pcre_uchar *compile_iterator_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;
pcre_uchar opcode;
pcre_uchar type;
int arg1 = -1, arg2 = -1;
pcre_uchar* end;
jump_list *nomatch = NULL;
struct sljit_jump *jump = NULL;
struct sljit_label *label;
int private_data_ptr = PRIVATE_DATA(cc);
int base = (private_data_ptr == 0) ? SLJIT_MEM1(STACK_TOP) : SLJIT_MEM1(SLJIT_LOCALS_REG);
int offset0 = (private_data_ptr == 0) ? STACK(0) : private_data_ptr;
int offset1 = (private_data_ptr == 0) ? STACK(1) : private_data_ptr + (int)sizeof(sljit_sw);
int tmp_base, tmp_offset;

PUSH_BACKTRACK(sizeof(iterator_backtrack), cc, NULL);

cc = get_iterator_parameters(common, cc, &opcode, &type, &arg1, &arg2, &end);

switch (type)
  {
  case OP_NOT_DIGIT:
  case OP_DIGIT:
  case OP_NOT_WHITESPACE:
  case OP_WHITESPACE:
  case OP_NOT_WORDCHAR:
  case OP_WORDCHAR:
  case OP_ANY:
  case OP_ALLANY:
  case OP_ANYBYTE:
  case OP_ANYNL:
  case OP_NOT_HSPACE:
  case OP_HSPACE:
  case OP_NOT_VSPACE:
  case OP_VSPACE:
  case OP_CHAR:
  case OP_CHARI:
  case OP_NOT:
  case OP_NOTI:
  case OP_CLASS:
  case OP_NCLASS:
  tmp_base = TMP3;
  tmp_offset = 0;
  break;

  default:
  SLJIT_ASSERT_STOP();
  /* Fall through. */

  case OP_EXTUNI:
  case OP_XCLASS:
  case OP_NOTPROP:
  case OP_PROP:
  tmp_base = SLJIT_MEM1(SLJIT_LOCALS_REG);
  tmp_offset = POSSESSIVE0;
  break;
  }

switch(opcode)
  {
  case OP_STAR:
  case OP_PLUS:
  case OP_UPTO:
  case OP_CRRANGE:
  if (type == OP_ANYNL || type == OP_EXTUNI)
    {
    SLJIT_ASSERT(private_data_ptr == 0);
    if (opcode == OP_STAR || opcode == OP_UPTO)
      {
      allocate_stack(common, 2);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), SLJIT_IMM, 0);
      }
    else
      {
      allocate_stack(common, 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      }

    if (opcode == OP_UPTO || opcode == OP_CRRANGE)
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0, SLJIT_IMM, 0);

    label = LABEL();
    compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
    if (opcode == OP_UPTO || opcode == OP_CRRANGE)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0);
      OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
      if (opcode == OP_CRRANGE && arg2 > 0)
        CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, arg2, label);
      if (opcode == OP_UPTO || (opcode == OP_CRRANGE && arg1 > 0))
        jump = CMP(SLJIT_C_GREATER_EQUAL, TMP1, 0, SLJIT_IMM, arg1);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE0, TMP1, 0);
      }

    /* We cannot use TMP3 because of this allocate_stack. */
    allocate_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
    JUMPTO(SLJIT_JUMP, label);
    if (jump != NULL)
      JUMPHERE(jump);
    }
  else
    {
    if (opcode == OP_PLUS)
      compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
    if (private_data_ptr == 0)
      allocate_stack(common, 2);
    OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
    if (opcode <= OP_PLUS)
      OP1(SLJIT_MOV, base, offset1, STR_PTR, 0);
    else
      OP1(SLJIT_MOV, base, offset1, SLJIT_IMM, 1);
    label = LABEL();
    compile_char1_matchingpath(common, type, cc, &nomatch);
    OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
    if (opcode <= OP_PLUS)
      JUMPTO(SLJIT_JUMP, label);
    else if (opcode == OP_CRRANGE && arg1 == 0)
      {
      OP2(SLJIT_ADD, base, offset1, base, offset1, SLJIT_IMM, 1);
      JUMPTO(SLJIT_JUMP, label);
      }
    else
      {
      OP1(SLJIT_MOV, TMP1, 0, base, offset1);
      OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
      OP1(SLJIT_MOV, base, offset1, TMP1, 0);
      CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, arg1 + 1, label);
      }
    set_jumps(nomatch, LABEL());
    if (opcode == OP_CRRANGE)
      add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_LESS, base, offset1, SLJIT_IMM, arg2 + 1));
    OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
    }
  BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();
  break;

  case OP_MINSTAR:
  case OP_MINPLUS:
  if (opcode == OP_MINPLUS)
    compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
  if (private_data_ptr == 0)
    allocate_stack(common, 1);
  OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
  BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();
  break;

  case OP_MINUPTO:
  case OP_CRMINRANGE:
  if (private_data_ptr == 0)
    allocate_stack(common, 2);
  OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
  OP1(SLJIT_MOV, base, offset1, SLJIT_IMM, 1);
  if (opcode == OP_CRMINRANGE)
    add_jump(compiler, &backtrack->topbacktracks, JUMP(SLJIT_JUMP));
  BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();
  break;

  case OP_QUERY:
  case OP_MINQUERY:
  if (private_data_ptr == 0)
    allocate_stack(common, 1);
  OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
  if (opcode == OP_QUERY)
    compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
  BACKTRACK_AS(iterator_backtrack)->matchingpath = LABEL();
  break;

  case OP_EXACT:
  OP1(SLJIT_MOV, tmp_base, tmp_offset, SLJIT_IMM, arg1);
  label = LABEL();
  compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
  OP2(SLJIT_SUB | SLJIT_SET_E, tmp_base, tmp_offset, tmp_base, tmp_offset, SLJIT_IMM, 1);
  JUMPTO(SLJIT_C_NOT_ZERO, label);
  break;

  case OP_POSSTAR:
  case OP_POSPLUS:
  case OP_POSUPTO:
  if (opcode == OP_POSPLUS)
    compile_char1_matchingpath(common, type, cc, &backtrack->topbacktracks);
  if (opcode == OP_POSUPTO)
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1, SLJIT_IMM, arg1);
  OP1(SLJIT_MOV, tmp_base, tmp_offset, STR_PTR, 0);
  label = LABEL();
  compile_char1_matchingpath(common, type, cc, &nomatch);
  OP1(SLJIT_MOV, tmp_base, tmp_offset, STR_PTR, 0);
  if (opcode != OP_POSUPTO)
    JUMPTO(SLJIT_JUMP, label);
  else
    {
    OP2(SLJIT_SUB | SLJIT_SET_E, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1, SLJIT_MEM1(SLJIT_LOCALS_REG), POSSESSIVE1, SLJIT_IMM, 1);
    JUMPTO(SLJIT_C_NOT_ZERO, label);
    }
  set_jumps(nomatch, LABEL());
  OP1(SLJIT_MOV, STR_PTR, 0, tmp_base, tmp_offset);
  break;

  case OP_POSQUERY:
  OP1(SLJIT_MOV, tmp_base, tmp_offset, STR_PTR, 0);
  compile_char1_matchingpath(common, type, cc, &nomatch);
  OP1(SLJIT_MOV, tmp_base, tmp_offset, STR_PTR, 0);
  set_jumps(nomatch, LABEL());
  OP1(SLJIT_MOV, STR_PTR, 0, tmp_base, tmp_offset);
  break;

  default:
  SLJIT_ASSERT_STOP();
  break;
  }

decrease_call_count(common);
return end;
}

static SLJIT_INLINE pcre_uchar *compile_fail_accept_matchingpath(compiler_common *common, pcre_uchar *cc, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;

PUSH_BACKTRACK(sizeof(bracket_backtrack), cc, NULL);

if (*cc == OP_FAIL)
  {
  add_jump(compiler, &backtrack->topbacktracks, JUMP(SLJIT_JUMP));
  return cc + 1;
  }

if (*cc == OP_ASSERT_ACCEPT || common->currententry != NULL)
  {
  /* No need to check notempty conditions. */
  if (common->acceptlabel == NULL)
    add_jump(compiler, &common->accept, JUMP(SLJIT_JUMP));
  else
    JUMPTO(SLJIT_JUMP, common->acceptlabel);
  return cc + 1;
  }

if (common->acceptlabel == NULL)
  add_jump(compiler, &common->accept, CMP(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0)));
else
  CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), common->acceptlabel);
OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, notempty));
add_jump(compiler, &backtrack->topbacktracks, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0));
OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, notempty_atstart));
if (common->acceptlabel == NULL)
  add_jump(compiler, &common->accept, CMP(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, 0));
else
  CMPTO(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, 0, common->acceptlabel);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
if (common->acceptlabel == NULL)
  add_jump(compiler, &common->accept, CMP(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_PTR, 0));
else
  CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_PTR, 0, common->acceptlabel);
add_jump(compiler, &backtrack->topbacktracks, JUMP(SLJIT_JUMP));
return cc + 1;
}

static SLJIT_INLINE pcre_uchar *compile_close_matchingpath(compiler_common *common, pcre_uchar *cc)
{
DEFINE_COMPILER;
int offset = GET2(cc, 1);
BOOL optimized_cbracket = common->optimized_cbracket[offset] != 0;

/* Data will be discarded anyway... */
if (common->currententry != NULL)
  return cc + 1 + IMM2_SIZE;

if (!optimized_cbracket)
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR_PRIV(offset));
offset <<= 1;
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), STR_PTR, 0);
if (!optimized_cbracket)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
return cc + 1 + IMM2_SIZE;
}

static void compile_matchingpath(compiler_common *common, pcre_uchar *cc, pcre_uchar *ccend, backtrack_common *parent)
{
DEFINE_COMPILER;
backtrack_common *backtrack;

while (cc < ccend)
  {
  switch(*cc)
    {
    case OP_SOD:
    case OP_SOM:
    case OP_NOT_WORD_BOUNDARY:
    case OP_WORD_BOUNDARY:
    case OP_NOT_DIGIT:
    case OP_DIGIT:
    case OP_NOT_WHITESPACE:
    case OP_WHITESPACE:
    case OP_NOT_WORDCHAR:
    case OP_WORDCHAR:
    case OP_ANY:
    case OP_ALLANY:
    case OP_ANYBYTE:
    case OP_NOTPROP:
    case OP_PROP:
    case OP_ANYNL:
    case OP_NOT_HSPACE:
    case OP_HSPACE:
    case OP_NOT_VSPACE:
    case OP_VSPACE:
    case OP_EXTUNI:
    case OP_EODN:
    case OP_EOD:
    case OP_CIRC:
    case OP_CIRCM:
    case OP_DOLL:
    case OP_DOLLM:
    case OP_NOT:
    case OP_NOTI:
    case OP_REVERSE:
    cc = compile_char1_matchingpath(common, *cc, cc + 1, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks);
    break;

    case OP_SET_SOM:
    PUSH_BACKTRACK_NOVALUE(sizeof(backtrack_common), cc);
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));
    allocate_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), STR_PTR, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP2, 0);
    cc++;
    break;

    case OP_CHAR:
    case OP_CHARI:
    if (common->mode == JIT_COMPILE)
      cc = compile_charn_matchingpath(common, cc, ccend, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks);
    else
      cc = compile_char1_matchingpath(common, *cc, cc + 1, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks);
    break;

    case OP_STAR:
    case OP_MINSTAR:
    case OP_PLUS:
    case OP_MINPLUS:
    case OP_QUERY:
    case OP_MINQUERY:
    case OP_UPTO:
    case OP_MINUPTO:
    case OP_EXACT:
    case OP_POSSTAR:
    case OP_POSPLUS:
    case OP_POSQUERY:
    case OP_POSUPTO:
    case OP_STARI:
    case OP_MINSTARI:
    case OP_PLUSI:
    case OP_MINPLUSI:
    case OP_QUERYI:
    case OP_MINQUERYI:
    case OP_UPTOI:
    case OP_MINUPTOI:
    case OP_EXACTI:
    case OP_POSSTARI:
    case OP_POSPLUSI:
    case OP_POSQUERYI:
    case OP_POSUPTOI:
    case OP_NOTSTAR:
    case OP_NOTMINSTAR:
    case OP_NOTPLUS:
    case OP_NOTMINPLUS:
    case OP_NOTQUERY:
    case OP_NOTMINQUERY:
    case OP_NOTUPTO:
    case OP_NOTMINUPTO:
    case OP_NOTEXACT:
    case OP_NOTPOSSTAR:
    case OP_NOTPOSPLUS:
    case OP_NOTPOSQUERY:
    case OP_NOTPOSUPTO:
    case OP_NOTSTARI:
    case OP_NOTMINSTARI:
    case OP_NOTPLUSI:
    case OP_NOTMINPLUSI:
    case OP_NOTQUERYI:
    case OP_NOTMINQUERYI:
    case OP_NOTUPTOI:
    case OP_NOTMINUPTOI:
    case OP_NOTEXACTI:
    case OP_NOTPOSSTARI:
    case OP_NOTPOSPLUSI:
    case OP_NOTPOSQUERYI:
    case OP_NOTPOSUPTOI:
    case OP_TYPESTAR:
    case OP_TYPEMINSTAR:
    case OP_TYPEPLUS:
    case OP_TYPEMINPLUS:
    case OP_TYPEQUERY:
    case OP_TYPEMINQUERY:
    case OP_TYPEUPTO:
    case OP_TYPEMINUPTO:
    case OP_TYPEEXACT:
    case OP_TYPEPOSSTAR:
    case OP_TYPEPOSPLUS:
    case OP_TYPEPOSQUERY:
    case OP_TYPEPOSUPTO:
    cc = compile_iterator_matchingpath(common, cc, parent);
    break;

    case OP_CLASS:
    case OP_NCLASS:
    if (cc[1 + (32 / sizeof(pcre_uchar))] >= OP_CRSTAR && cc[1 + (32 / sizeof(pcre_uchar))] <= OP_CRMINRANGE)
      cc = compile_iterator_matchingpath(common, cc, parent);
    else
      cc = compile_char1_matchingpath(common, *cc, cc + 1, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks);
    break;

#if defined SUPPORT_UTF || defined COMPILE_PCRE16 || defined COMPILE_PCRE32
    case OP_XCLASS:
    if (*(cc + GET(cc, 1)) >= OP_CRSTAR && *(cc + GET(cc, 1)) <= OP_CRMINRANGE)
      cc = compile_iterator_matchingpath(common, cc, parent);
    else
      cc = compile_char1_matchingpath(common, *cc, cc + 1, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks);
    break;
#endif

    case OP_REF:
    case OP_REFI:
    if (cc[1 + IMM2_SIZE] >= OP_CRSTAR && cc[1 + IMM2_SIZE] <= OP_CRMINRANGE)
      cc = compile_ref_iterator_matchingpath(common, cc, parent);
    else
      cc = compile_ref_matchingpath(common, cc, parent->top != NULL ? &parent->top->nextbacktracks : &parent->topbacktracks, TRUE, FALSE);
    break;

    case OP_RECURSE:
    cc = compile_recurse_matchingpath(common, cc, parent);
    break;

    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    PUSH_BACKTRACK_NOVALUE(sizeof(assert_backtrack), cc);
    cc = compile_assert_matchingpath(common, cc, BACKTRACK_AS(assert_backtrack), FALSE);
    break;

    case OP_BRAMINZERO:
    PUSH_BACKTRACK_NOVALUE(sizeof(braminzero_backtrack), cc);
    cc = bracketend(cc + 1);
    if (*(cc - 1 - LINK_SIZE) != OP_KETRMIN)
      {
      allocate_stack(common, 1);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);
      }
    else
      {
      allocate_stack(common, 2);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(1), STR_PTR, 0);
      }
    BACKTRACK_AS(braminzero_backtrack)->matchingpath = LABEL();
    if (cc[1] > OP_ASSERTBACK_NOT)
      decrease_call_count(common);
    break;

    case OP_ONCE:
    case OP_ONCE_NC:
    case OP_BRA:
    case OP_CBRA:
    case OP_COND:
    case OP_SBRA:
    case OP_SCBRA:
    case OP_SCOND:
    cc = compile_bracket_matchingpath(common, cc, parent);
    break;

    case OP_BRAZERO:
    if (cc[1] > OP_ASSERTBACK_NOT)
      cc = compile_bracket_matchingpath(common, cc, parent);
    else
      {
      PUSH_BACKTRACK_NOVALUE(sizeof(assert_backtrack), cc);
      cc = compile_assert_matchingpath(common, cc, BACKTRACK_AS(assert_backtrack), FALSE);
      }
    break;

    case OP_BRAPOS:
    case OP_CBRAPOS:
    case OP_SBRAPOS:
    case OP_SCBRAPOS:
    case OP_BRAPOSZERO:
    cc = compile_bracketpos_matchingpath(common, cc, parent);
    break;

    case OP_MARK:
    PUSH_BACKTRACK_NOVALUE(sizeof(backtrack_common), cc);
    SLJIT_ASSERT(common->mark_ptr != 0);
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr);
    allocate_stack(common, 1);
    OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), TMP2, 0);
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_IMM, (sljit_sw)(cc + 2));
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr, TMP2, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, mark_ptr), TMP2, 0);
    cc += 1 + 2 + cc[1];
    break;

    case OP_COMMIT:
    PUSH_BACKTRACK_NOVALUE(sizeof(backtrack_common), cc);
    cc += 1;
    break;

    case OP_FAIL:
    case OP_ACCEPT:
    case OP_ASSERT_ACCEPT:
    cc = compile_fail_accept_matchingpath(common, cc, parent);
    break;

    case OP_CLOSE:
    cc = compile_close_matchingpath(common, cc);
    break;

    case OP_SKIPZERO:
    cc = bracketend(cc + 1);
    break;

    default:
    SLJIT_ASSERT_STOP();
    return;
    }
  if (cc == NULL)
    return;
  }
SLJIT_ASSERT(cc == ccend);
}

#undef PUSH_BACKTRACK
#undef PUSH_BACKTRACK_NOVALUE
#undef BACKTRACK_AS

#define COMPILE_BACKTRACKINGPATH(current) \
  do \
    { \
    compile_backtrackingpath(common, (current)); \
    if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler))) \
      return; \
    } \
  while (0)

#define CURRENT_AS(type) ((type *)current)

static void compile_iterator_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;
pcre_uchar *cc = current->cc;
pcre_uchar opcode;
pcre_uchar type;
int arg1 = -1, arg2 = -1;
struct sljit_label *label = NULL;
struct sljit_jump *jump = NULL;
jump_list *jumplist = NULL;
int private_data_ptr = PRIVATE_DATA(cc);
int base = (private_data_ptr == 0) ? SLJIT_MEM1(STACK_TOP) : SLJIT_MEM1(SLJIT_LOCALS_REG);
int offset0 = (private_data_ptr == 0) ? STACK(0) : private_data_ptr;
int offset1 = (private_data_ptr == 0) ? STACK(1) : private_data_ptr + (int)sizeof(sljit_sw);

cc = get_iterator_parameters(common, cc, &opcode, &type, &arg1, &arg2, NULL);

switch(opcode)
  {
  case OP_STAR:
  case OP_PLUS:
  case OP_UPTO:
  case OP_CRRANGE:
  if (type == OP_ANYNL || type == OP_EXTUNI)
    {
    SLJIT_ASSERT(private_data_ptr == 0);
    set_jumps(current->topbacktracks, LABEL());
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    free_stack(common, 1);
    CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(iterator_backtrack)->matchingpath);
    }
  else
    {
    if (opcode == OP_UPTO)
      arg2 = 0;
    if (opcode <= OP_PLUS)
      {
      OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
      jump = CMP(SLJIT_C_LESS_EQUAL, STR_PTR, 0, base, offset1);
      }
    else
      {
      OP1(SLJIT_MOV, TMP1, 0, base, offset1);
      OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
      jump = CMP(SLJIT_C_LESS_EQUAL, TMP1, 0, SLJIT_IMM, arg2 + 1);
      OP2(SLJIT_SUB, base, offset1, TMP1, 0, SLJIT_IMM, 1);
      }
    skip_char_back(common);
    OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
    JUMPTO(SLJIT_JUMP, CURRENT_AS(iterator_backtrack)->matchingpath);
    if (opcode == OP_CRRANGE)
      set_jumps(current->topbacktracks, LABEL());
    JUMPHERE(jump);
    if (private_data_ptr == 0)
      free_stack(common, 2);
    if (opcode == OP_PLUS)
      set_jumps(current->topbacktracks, LABEL());
    }
  break;

  case OP_MINSTAR:
  case OP_MINPLUS:
  OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
  compile_char1_matchingpath(common, type, cc, &jumplist);
  OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
  JUMPTO(SLJIT_JUMP, CURRENT_AS(iterator_backtrack)->matchingpath);
  set_jumps(jumplist, LABEL());
  if (private_data_ptr == 0)
    free_stack(common, 1);
  if (opcode == OP_MINPLUS)
    set_jumps(current->topbacktracks, LABEL());
  break;

  case OP_MINUPTO:
  case OP_CRMINRANGE:
  if (opcode == OP_CRMINRANGE)
    {
    label = LABEL();
    set_jumps(current->topbacktracks, label);
    }
  OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
  compile_char1_matchingpath(common, type, cc, &jumplist);

  OP1(SLJIT_MOV, TMP1, 0, base, offset1);
  OP1(SLJIT_MOV, base, offset0, STR_PTR, 0);
  OP2(SLJIT_ADD, TMP1, 0, TMP1, 0, SLJIT_IMM, 1);
  OP1(SLJIT_MOV, base, offset1, TMP1, 0);

  if (opcode == OP_CRMINRANGE)
    CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, arg2 + 1, label);

  if (opcode == OP_CRMINRANGE && arg1 == 0)
    JUMPTO(SLJIT_JUMP, CURRENT_AS(iterator_backtrack)->matchingpath);
  else
    CMPTO(SLJIT_C_LESS, TMP1, 0, SLJIT_IMM, arg1 + 2, CURRENT_AS(iterator_backtrack)->matchingpath);

  set_jumps(jumplist, LABEL());
  if (private_data_ptr == 0)
    free_stack(common, 2);
  break;

  case OP_QUERY:
  OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
  OP1(SLJIT_MOV, base, offset0, SLJIT_IMM, 0);
  CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(iterator_backtrack)->matchingpath);
  jump = JUMP(SLJIT_JUMP);
  set_jumps(current->topbacktracks, LABEL());
  OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
  OP1(SLJIT_MOV, base, offset0, SLJIT_IMM, 0);
  JUMPTO(SLJIT_JUMP, CURRENT_AS(iterator_backtrack)->matchingpath);
  JUMPHERE(jump);
  if (private_data_ptr == 0)
    free_stack(common, 1);
  break;

  case OP_MINQUERY:
  OP1(SLJIT_MOV, STR_PTR, 0, base, offset0);
  OP1(SLJIT_MOV, base, offset0, SLJIT_IMM, 0);
  jump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
  compile_char1_matchingpath(common, type, cc, &jumplist);
  JUMPTO(SLJIT_JUMP, CURRENT_AS(iterator_backtrack)->matchingpath);
  set_jumps(jumplist, LABEL());
  JUMPHERE(jump);
  if (private_data_ptr == 0)
    free_stack(common, 1);
  break;

  case OP_EXACT:
  case OP_POSPLUS:
  set_jumps(current->topbacktracks, LABEL());
  break;

  case OP_POSSTAR:
  case OP_POSQUERY:
  case OP_POSUPTO:
  break;

  default:
  SLJIT_ASSERT_STOP();
  break;
  }
}

static void compile_ref_iterator_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;
pcre_uchar *cc = current->cc;
pcre_uchar type;

type = cc[1 + IMM2_SIZE];
if ((type & 0x1) == 0)
  {
  set_jumps(current->topbacktracks, LABEL());
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(iterator_backtrack)->matchingpath);
  return;
  }

OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(iterator_backtrack)->matchingpath);
set_jumps(current->topbacktracks, LABEL());
free_stack(common, 2);
}

static void compile_recurse_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;

set_jumps(current->topbacktracks, LABEL());

if (common->has_set_som && common->mark_ptr != 0)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
  free_stack(common, 2);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), TMP2, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr, TMP1, 0);
  }
else if (common->has_set_som || common->mark_ptr != 0)
  {
  OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->has_set_som ? (int)(OVECTOR(0)) : common->mark_ptr, TMP2, 0);
  }
}

static void compile_assert_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;
pcre_uchar *cc = current->cc;
pcre_uchar bra = OP_BRA;
struct sljit_jump *brajump = NULL;

SLJIT_ASSERT(*cc != OP_BRAMINZERO);
if (*cc == OP_BRAZERO)
  {
  bra = *cc;
  cc++;
  }

if (bra == OP_BRAZERO)
  {
  SLJIT_ASSERT(current->topbacktracks == NULL);
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  }

if (CURRENT_AS(assert_backtrack)->framesize < 0)
  {
  set_jumps(current->topbacktracks, LABEL());

  if (bra == OP_BRAZERO)
    {
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
    CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(assert_backtrack)->matchingpath);
    free_stack(common, 1);
    }
  return;
  }

if (bra == OP_BRAZERO)
  {
  if (*cc == OP_ASSERT_NOT || *cc == OP_ASSERTBACK_NOT)
    {
    OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
    CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(assert_backtrack)->matchingpath);
    free_stack(common, 1);
    return;
    }
  free_stack(common, 1);
  brajump = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_IMM, 0);
  }

if (*cc == OP_ASSERT || *cc == OP_ASSERTBACK)
  {
  OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), CURRENT_AS(assert_backtrack)->private_data_ptr);
  add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), CURRENT_AS(assert_backtrack)->private_data_ptr, SLJIT_MEM1(STACK_TOP), CURRENT_AS(assert_backtrack)->framesize * sizeof(sljit_sw));

  set_jumps(current->topbacktracks, LABEL());
  }
else
  set_jumps(current->topbacktracks, LABEL());

if (bra == OP_BRAZERO)
  {
  /* We know there is enough place on the stack. */
  OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), SLJIT_IMM, 0);
  JUMPTO(SLJIT_JUMP, CURRENT_AS(assert_backtrack)->matchingpath);
  JUMPHERE(brajump);
  }
}

static void compile_bracket_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;
int opcode;
int offset = 0;
int private_data_ptr = CURRENT_AS(bracket_backtrack)->private_data_ptr;
int stacksize;
int count;
pcre_uchar *cc = current->cc;
pcre_uchar *ccbegin;
pcre_uchar *ccprev;
jump_list *jumplist = NULL;
jump_list *jumplistitem = NULL;
pcre_uchar bra = OP_BRA;
pcre_uchar ket;
assert_backtrack *assert;
BOOL has_alternatives;
struct sljit_jump *brazero = NULL;
struct sljit_jump *once = NULL;
struct sljit_jump *cond = NULL;
struct sljit_label *rminlabel = NULL;

if (*cc == OP_BRAZERO || *cc == OP_BRAMINZERO)
  {
  bra = *cc;
  cc++;
  }

opcode = *cc;
ccbegin = cc;
ket = *(bracketend(ccbegin) - 1 - LINK_SIZE);
cc += GET(cc, 1);
has_alternatives = *cc == OP_ALT;
if (SLJIT_UNLIKELY(opcode == OP_COND) || SLJIT_UNLIKELY(opcode == OP_SCOND))
  has_alternatives = (ccbegin[1 + LINK_SIZE] >= OP_ASSERT && ccbegin[1 + LINK_SIZE] <= OP_ASSERTBACK_NOT) || CURRENT_AS(bracket_backtrack)->u.condfailed != NULL;
if (opcode == OP_CBRA || opcode == OP_SCBRA)
  offset = (GET2(ccbegin, 1 + LINK_SIZE)) << 1;
if (SLJIT_UNLIKELY(opcode == OP_COND) && (*cc == OP_KETRMAX || *cc == OP_KETRMIN))
  opcode = OP_SCOND;
if (SLJIT_UNLIKELY(opcode == OP_ONCE_NC))
  opcode = OP_ONCE;

if (ket == OP_KETRMAX)
  {
  if (bra == OP_BRAZERO)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    free_stack(common, 1);
    brazero = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, 0);
    }
  }
else if (ket == OP_KETRMIN)
  {
  if (bra != OP_BRAMINZERO)
    {
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    if (opcode >= OP_SBRA || opcode == OP_ONCE)
      {
      /* Checking zero-length iteration. */
      if (opcode != OP_ONCE || CURRENT_AS(bracket_backtrack)->u.framesize < 0)
        CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, CURRENT_AS(bracket_backtrack)->recursive_matchingpath);
      else
        {
        OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
        CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_MEM1(TMP1), (CURRENT_AS(bracket_backtrack)->u.framesize + 1) * sizeof(sljit_sw), CURRENT_AS(bracket_backtrack)->recursive_matchingpath);
        }
      if (opcode != OP_ONCE)
        free_stack(common, 1);
      }
    else
      JUMPTO(SLJIT_JUMP, CURRENT_AS(bracket_backtrack)->recursive_matchingpath);
    }
  rminlabel = LABEL();
  }
else if (bra == OP_BRAZERO)
  {
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  brazero = CMP(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, 0);
  }

if (SLJIT_UNLIKELY(opcode == OP_ONCE))
  {
  if (CURRENT_AS(bracket_backtrack)->u.framesize >= 0)
    {
    OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
    add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
    }
  once = JUMP(SLJIT_JUMP);
  }
else if (SLJIT_UNLIKELY(opcode == OP_COND) || SLJIT_UNLIKELY(opcode == OP_SCOND))
  {
  if (has_alternatives)
    {
    /* Always exactly one alternative. */
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    free_stack(common, 1);

    jumplistitem = sljit_alloc_memory(compiler, sizeof(jump_list));
    if (SLJIT_UNLIKELY(!jumplistitem))
      return;
    jumplist = jumplistitem;
    jumplistitem->next = NULL;
    jumplistitem->jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, 1);
    }
  }
else if (*cc == OP_ALT)
  {
  /* Build a jump list. Get the last successfully matched branch index. */
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  count = 1;
  do
    {
    /* Append as the last item. */
    if (jumplist != NULL)
      {
      jumplistitem->next = sljit_alloc_memory(compiler, sizeof(jump_list));
      jumplistitem = jumplistitem->next;
      }
    else
      {
      jumplistitem = sljit_alloc_memory(compiler, sizeof(jump_list));
      jumplist = jumplistitem;
      }

    if (SLJIT_UNLIKELY(!jumplistitem))
      return;

    jumplistitem->next = NULL;
    jumplistitem->jump = CMP(SLJIT_C_EQUAL, TMP1, 0, SLJIT_IMM, count++);
    cc += GET(cc, 1);
    }
  while (*cc == OP_ALT);

  cc = ccbegin + GET(ccbegin, 1);
  }

COMPILE_BACKTRACKINGPATH(current->top);
if (current->topbacktracks)
  set_jumps(current->topbacktracks, LABEL());

if (SLJIT_UNLIKELY(opcode == OP_COND) || SLJIT_UNLIKELY(opcode == OP_SCOND))
  {
  /* Conditional block always has at most one alternative. */
  if (ccbegin[1 + LINK_SIZE] >= OP_ASSERT && ccbegin[1 + LINK_SIZE] <= OP_ASSERTBACK_NOT)
    {
    SLJIT_ASSERT(has_alternatives);
    assert = CURRENT_AS(bracket_backtrack)->u.assert;
    if (assert->framesize >= 0 && (ccbegin[1 + LINK_SIZE] == OP_ASSERT || ccbegin[1 + LINK_SIZE] == OP_ASSERTBACK))
      {
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), assert->private_data_ptr);
      add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), assert->private_data_ptr, SLJIT_MEM1(STACK_TOP), assert->framesize * sizeof(sljit_sw));
      }
    cond = JUMP(SLJIT_JUMP);
    set_jumps(CURRENT_AS(bracket_backtrack)->u.assert->condfailed, LABEL());
    }
  else if (CURRENT_AS(bracket_backtrack)->u.condfailed != NULL)
    {
    SLJIT_ASSERT(has_alternatives);
    cond = JUMP(SLJIT_JUMP);
    set_jumps(CURRENT_AS(bracket_backtrack)->u.condfailed, LABEL());
    }
  else
    SLJIT_ASSERT(!has_alternatives);
  }

if (has_alternatives)
  {
  count = 1;
  do
    {
    current->top = NULL;
    current->topbacktracks = NULL;
    current->nextbacktracks = NULL;
    if (*cc == OP_ALT)
      {
      ccprev = cc + 1 + LINK_SIZE;
      cc += GET(cc, 1);
      if (opcode != OP_COND && opcode != OP_SCOND)
        {
        if (private_data_ptr != 0 && opcode != OP_ONCE)
          OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
        else
          OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
        }
      compile_matchingpath(common, ccprev, cc, current);
      if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
        return;
      }

    /* Instructions after the current alternative is succesfully matched. */
    /* There is a similar code in compile_bracket_matchingpath. */
    if (opcode == OP_ONCE)
      {
      if (CURRENT_AS(bracket_backtrack)->u.framesize < 0)
        {
        OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
        /* TMP2 which is set here used by OP_KETRMAX below. */
        if (ket == OP_KETRMAX)
          OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), 0);
        else if (ket == OP_KETRMIN)
          {
          /* Move the STR_PTR to the private_data_ptr. */
          OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), 0);
          }
        }
      else
        {
        OP2(SLJIT_ADD, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_IMM, (CURRENT_AS(bracket_backtrack)->u.framesize + 2) * sizeof(sljit_sw));
        if (ket == OP_KETRMAX)
          {
          /* TMP2 which is set here used by OP_KETRMAX below. */
          OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
          }
        }
      }

    stacksize = 0;
    if (opcode != OP_ONCE)
      stacksize++;
    if (ket != OP_KET || bra != OP_BRA)
      stacksize++;

    if (stacksize > 0) {
      if (opcode != OP_ONCE || CURRENT_AS(bracket_backtrack)->u.framesize >= 0)
        allocate_stack(common, stacksize);
      else
        {
        /* We know we have place at least for one item on the top of the stack. */
        SLJIT_ASSERT(stacksize == 1);
        OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, sizeof(sljit_sw));
        }
    }

    stacksize = 0;
    if (ket != OP_KET || bra != OP_BRA)
      {
      if (ket != OP_KET)
        OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), STR_PTR, 0);
      else
        OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), SLJIT_IMM, 0);
      stacksize++;
      }

    if (opcode != OP_ONCE)
      OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(stacksize), SLJIT_IMM, count++);

    if (offset != 0)
      {
      OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), STR_PTR, 0);
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 0), TMP1, 0);
      }

    JUMPTO(SLJIT_JUMP, CURRENT_AS(bracket_backtrack)->alternative_matchingpath);

    if (opcode != OP_ONCE)
      {
      SLJIT_ASSERT(jumplist);
      JUMPHERE(jumplist->jump);
      jumplist = jumplist->next;
      }

    COMPILE_BACKTRACKINGPATH(current->top);
    if (current->topbacktracks)
      set_jumps(current->topbacktracks, LABEL());
    SLJIT_ASSERT(!current->nextbacktracks);
    }
  while (*cc == OP_ALT);
  SLJIT_ASSERT(!jumplist);

  if (cond != NULL)
    {
    SLJIT_ASSERT(opcode == OP_COND || opcode == OP_SCOND);
    assert = CURRENT_AS(bracket_backtrack)->u.assert;
    if ((ccbegin[1 + LINK_SIZE] == OP_ASSERT_NOT || ccbegin[1 + LINK_SIZE] == OP_ASSERTBACK_NOT) && assert->framesize >= 0)

      {
      OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), assert->private_data_ptr);
      add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
      OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), assert->private_data_ptr, SLJIT_MEM1(STACK_TOP), assert->framesize * sizeof(sljit_sw));
      }
    JUMPHERE(cond);
    }

  /* Free the STR_PTR. */
  if (private_data_ptr == 0)
    free_stack(common, 1);
  }

if (offset != 0)
  {
  /* Using both tmp register is better for instruction scheduling. */
  if (common->optimized_cbracket[offset >> 1] == 0)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(2));
    free_stack(common, 3);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), TMP2, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP1, 0);
    }
  else
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    free_stack(common, 2);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), TMP2, 0);
    }
  }
else if (opcode == OP_SBRA || opcode == OP_SCOND)
  {
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), STACK(0));
  free_stack(common, 1);
  }
else if (opcode == OP_ONCE)
  {
  cc = ccbegin + GET(ccbegin, 1);
  if (CURRENT_AS(bracket_backtrack)->u.framesize >= 0)
    {
    /* Reset head and drop saved frame. */
    stacksize = (ket == OP_KETRMAX || ket == OP_KETRMIN || *cc == OP_ALT) ? 2 : 1;
    free_stack(common, CURRENT_AS(bracket_backtrack)->u.framesize + stacksize);
    }
  else if (ket == OP_KETRMAX || (*cc == OP_ALT && ket != OP_KETRMIN))
    {
    /* The STR_PTR must be released. */
    free_stack(common, 1);
    }

  JUMPHERE(once);
  /* Restore previous private_data_ptr */
  if (CURRENT_AS(bracket_backtrack)->u.framesize >= 0)
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, SLJIT_MEM1(STACK_TOP), CURRENT_AS(bracket_backtrack)->u.framesize * sizeof(sljit_sw));
  else if (ket == OP_KETRMIN)
    {
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    /* See the comment below. */
    free_stack(common, 2);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), private_data_ptr, TMP1, 0);
    }
  }

if (ket == OP_KETRMAX)
  {
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  if (bra != OP_BRAZERO)
    free_stack(common, 1);
  CMPTO(SLJIT_C_NOT_EQUAL, STR_PTR, 0, SLJIT_IMM, 0, CURRENT_AS(bracket_backtrack)->recursive_matchingpath);
  if (bra == OP_BRAZERO)
    {
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    JUMPTO(SLJIT_JUMP, CURRENT_AS(bracket_backtrack)->zero_matchingpath);
    JUMPHERE(brazero);
    free_stack(common, 1);
    }
  }
else if (ket == OP_KETRMIN)
  {
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));

  /* OP_ONCE removes everything in case of a backtrack, so we don't
  need to explicitly release the STR_PTR. The extra release would
  affect badly the free_stack(2) above. */
  if (opcode != OP_ONCE)
    free_stack(common, 1);
  CMPTO(SLJIT_C_NOT_EQUAL, TMP1, 0, SLJIT_IMM, 0, rminlabel);
  if (opcode == OP_ONCE)
    free_stack(common, bra == OP_BRAMINZERO ? 2 : 1);
  else if (bra == OP_BRAMINZERO)
    free_stack(common, 1);
  }
else if (bra == OP_BRAZERO)
  {
  OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
  JUMPTO(SLJIT_JUMP, CURRENT_AS(bracket_backtrack)->zero_matchingpath);
  JUMPHERE(brazero);
  }
}

static void compile_bracketpos_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;
int offset;
struct sljit_jump *jump;

if (CURRENT_AS(bracketpos_backtrack)->framesize < 0)
  {
  if (*current->cc == OP_CBRAPOS || *current->cc == OP_SCBRAPOS)
    {
    offset = (GET2(current->cc, 1 + LINK_SIZE)) << 1;
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), STACK(1));
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset), TMP1, 0);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(offset + 1), TMP2, 0);
    }
  set_jumps(current->topbacktracks, LABEL());
  free_stack(common, CURRENT_AS(bracketpos_backtrack)->stacksize);
  return;
  }

OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), CURRENT_AS(bracketpos_backtrack)->private_data_ptr);
add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));

if (current->topbacktracks)
  {
  jump = JUMP(SLJIT_JUMP);
  set_jumps(current->topbacktracks, LABEL());
  /* Drop the stack frame. */
  free_stack(common, CURRENT_AS(bracketpos_backtrack)->stacksize);
  JUMPHERE(jump);
  }
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), CURRENT_AS(bracketpos_backtrack)->private_data_ptr, SLJIT_MEM1(STACK_TOP), CURRENT_AS(bracketpos_backtrack)->framesize * sizeof(sljit_sw));
}

static void compile_braminzero_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
assert_backtrack backtrack;

current->top = NULL;
current->topbacktracks = NULL;
current->nextbacktracks = NULL;
if (current->cc[1] > OP_ASSERTBACK_NOT)
  {
  /* Manual call of compile_bracket_matchingpath and compile_bracket_backtrackingpath. */
  compile_bracket_matchingpath(common, current->cc, current);
  compile_bracket_backtrackingpath(common, current->top);
  }
else
  {
  memset(&backtrack, 0, sizeof(backtrack));
  backtrack.common.cc = current->cc;
  backtrack.matchingpath = CURRENT_AS(braminzero_backtrack)->matchingpath;
  /* Manual call of compile_assert_matchingpath. */
  compile_assert_matchingpath(common, current->cc, &backtrack, FALSE);
  }
SLJIT_ASSERT(!current->nextbacktracks && !current->topbacktracks);
}

static void compile_backtrackingpath(compiler_common *common, struct backtrack_common *current)
{
DEFINE_COMPILER;

while (current)
  {
  if (current->nextbacktracks != NULL)
    set_jumps(current->nextbacktracks, LABEL());
  switch(*current->cc)
    {
    case OP_SET_SOM:
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    free_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), TMP1, 0);
    break;

    case OP_STAR:
    case OP_MINSTAR:
    case OP_PLUS:
    case OP_MINPLUS:
    case OP_QUERY:
    case OP_MINQUERY:
    case OP_UPTO:
    case OP_MINUPTO:
    case OP_EXACT:
    case OP_POSSTAR:
    case OP_POSPLUS:
    case OP_POSQUERY:
    case OP_POSUPTO:
    case OP_STARI:
    case OP_MINSTARI:
    case OP_PLUSI:
    case OP_MINPLUSI:
    case OP_QUERYI:
    case OP_MINQUERYI:
    case OP_UPTOI:
    case OP_MINUPTOI:
    case OP_EXACTI:
    case OP_POSSTARI:
    case OP_POSPLUSI:
    case OP_POSQUERYI:
    case OP_POSUPTOI:
    case OP_NOTSTAR:
    case OP_NOTMINSTAR:
    case OP_NOTPLUS:
    case OP_NOTMINPLUS:
    case OP_NOTQUERY:
    case OP_NOTMINQUERY:
    case OP_NOTUPTO:
    case OP_NOTMINUPTO:
    case OP_NOTEXACT:
    case OP_NOTPOSSTAR:
    case OP_NOTPOSPLUS:
    case OP_NOTPOSQUERY:
    case OP_NOTPOSUPTO:
    case OP_NOTSTARI:
    case OP_NOTMINSTARI:
    case OP_NOTPLUSI:
    case OP_NOTMINPLUSI:
    case OP_NOTQUERYI:
    case OP_NOTMINQUERYI:
    case OP_NOTUPTOI:
    case OP_NOTMINUPTOI:
    case OP_NOTEXACTI:
    case OP_NOTPOSSTARI:
    case OP_NOTPOSPLUSI:
    case OP_NOTPOSQUERYI:
    case OP_NOTPOSUPTOI:
    case OP_TYPESTAR:
    case OP_TYPEMINSTAR:
    case OP_TYPEPLUS:
    case OP_TYPEMINPLUS:
    case OP_TYPEQUERY:
    case OP_TYPEMINQUERY:
    case OP_TYPEUPTO:
    case OP_TYPEMINUPTO:
    case OP_TYPEEXACT:
    case OP_TYPEPOSSTAR:
    case OP_TYPEPOSPLUS:
    case OP_TYPEPOSQUERY:
    case OP_TYPEPOSUPTO:
    case OP_CLASS:
    case OP_NCLASS:
#if defined SUPPORT_UTF || !defined COMPILE_PCRE8
    case OP_XCLASS:
#endif
    compile_iterator_backtrackingpath(common, current);
    break;

    case OP_REF:
    case OP_REFI:
    compile_ref_iterator_backtrackingpath(common, current);
    break;

    case OP_RECURSE:
    compile_recurse_backtrackingpath(common, current);
    break;

    case OP_ASSERT:
    case OP_ASSERT_NOT:
    case OP_ASSERTBACK:
    case OP_ASSERTBACK_NOT:
    compile_assert_backtrackingpath(common, current);
    break;

    case OP_ONCE:
    case OP_ONCE_NC:
    case OP_BRA:
    case OP_CBRA:
    case OP_COND:
    case OP_SBRA:
    case OP_SCBRA:
    case OP_SCOND:
    compile_bracket_backtrackingpath(common, current);
    break;

    case OP_BRAZERO:
    if (current->cc[1] > OP_ASSERTBACK_NOT)
      compile_bracket_backtrackingpath(common, current);
    else
      compile_assert_backtrackingpath(common, current);
    break;

    case OP_BRAPOS:
    case OP_CBRAPOS:
    case OP_SBRAPOS:
    case OP_SCBRAPOS:
    case OP_BRAPOSZERO:
    compile_bracketpos_backtrackingpath(common, current);
    break;

    case OP_BRAMINZERO:
    compile_braminzero_backtrackingpath(common, current);
    break;

    case OP_MARK:
    OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(STACK_TOP), STACK(0));
    free_stack(common, 1);
    OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr, TMP1, 0);
    break;

    case OP_COMMIT:
    OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, PCRE_ERROR_NOMATCH);
    if (common->quitlabel == NULL)
      add_jump(compiler, &common->quit, JUMP(SLJIT_JUMP));
    else
      JUMPTO(SLJIT_JUMP, common->quitlabel);
    break;

    case OP_FAIL:
    case OP_ACCEPT:
    case OP_ASSERT_ACCEPT:
    set_jumps(current->topbacktracks, LABEL());
    break;

    default:
    SLJIT_ASSERT_STOP();
    break;
    }
  current = current->prev;
  }
}

static SLJIT_INLINE void compile_recurse(compiler_common *common)
{
DEFINE_COMPILER;
pcre_uchar *cc = common->start + common->currententry->start;
pcre_uchar *ccbegin = cc + 1 + LINK_SIZE + (*cc == OP_BRA ? 0 : IMM2_SIZE);
pcre_uchar *ccend = bracketend(cc);
int private_data_size = get_private_data_length_for_copy(common, ccbegin, ccend);
int framesize = get_framesize(common, cc, TRUE);
int alternativesize;
BOOL needsframe;
backtrack_common altbacktrack;
struct sljit_label *save_quitlabel = common->quitlabel;
jump_list *save_quit = common->quit;
struct sljit_jump *jump;

SLJIT_ASSERT(*cc == OP_BRA || *cc == OP_CBRA || *cc == OP_CBRAPOS || *cc == OP_SCBRA || *cc == OP_SCBRAPOS);
needsframe = framesize >= 0;
if (!needsframe)
  framesize = 0;
alternativesize = *(cc + GET(cc, 1)) == OP_ALT ? 1 : 0;

SLJIT_ASSERT(common->currententry->entry == NULL && common->recursive_head != 0);
common->currententry->entry = LABEL();
set_jumps(common->currententry->calls, common->currententry->entry);

sljit_emit_fast_enter(compiler, TMP2, 0);
allocate_stack(common, private_data_size + framesize + alternativesize);
OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(private_data_size + framesize + alternativesize - 1), TMP2, 0);
copy_private_data(common, ccbegin, ccend, TRUE, private_data_size + framesize + alternativesize, framesize + alternativesize);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->recursive_head, STACK_TOP, 0);
if (needsframe)
  init_frame(common, cc, framesize + alternativesize - 1, alternativesize, TRUE);

if (alternativesize > 0)
  OP1(SLJIT_MOV, SLJIT_MEM1(STACK_TOP), STACK(0), STR_PTR, 0);

memset(&altbacktrack, 0, sizeof(backtrack_common));
common->quitlabel = NULL;
common->acceptlabel = NULL;
common->quit = NULL;
common->accept = NULL;
altbacktrack.cc = ccbegin;
cc += GET(cc, 1);
while (1)
  {
  altbacktrack.top = NULL;
  altbacktrack.topbacktracks = NULL;

  if (altbacktrack.cc != ccbegin)
    OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(STACK_TOP), STACK(0));

  compile_matchingpath(common, altbacktrack.cc, cc, &altbacktrack);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    {
    common->quitlabel = save_quitlabel;
    common->quit = save_quit;
    return;
    }

  add_jump(compiler, &common->accept, JUMP(SLJIT_JUMP));

  compile_backtrackingpath(common, altbacktrack.top);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    {
    common->quitlabel = save_quitlabel;
    common->quit = save_quit;
    return;
    }
  set_jumps(altbacktrack.topbacktracks, LABEL());

  if (*cc != OP_ALT)
    break;

  altbacktrack.cc = cc + 1 + LINK_SIZE;
  cc += GET(cc, 1);
  }
/* None of them matched. */
if (common->quit != NULL)
  set_jumps(common->quit, LABEL());

OP1(SLJIT_MOV, TMP3, 0, SLJIT_IMM, 0);
jump = JUMP(SLJIT_JUMP);

set_jumps(common->accept, LABEL());
OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->recursive_head);
if (needsframe)
  {
  OP2(SLJIT_SUB, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, (framesize + alternativesize) * sizeof(sljit_sw));
  add_jump(compiler, &common->revertframes, JUMP(SLJIT_FAST_CALL));
  OP2(SLJIT_ADD, STACK_TOP, 0, STACK_TOP, 0, SLJIT_IMM, (framesize + alternativesize) * sizeof(sljit_sw));
  }
OP1(SLJIT_MOV, TMP3, 0, SLJIT_IMM, 1);

JUMPHERE(jump);
copy_private_data(common, ccbegin, ccend, FALSE, private_data_size + framesize + alternativesize, framesize + alternativesize);
free_stack(common, private_data_size + framesize + alternativesize);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(STACK_TOP), sizeof(sljit_sw));
OP1(SLJIT_MOV, TMP1, 0, TMP3, 0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->recursive_head, TMP2, 0);
sljit_emit_fast_return(compiler, SLJIT_MEM1(STACK_TOP), 0);

common->quitlabel = save_quitlabel;
common->quit = save_quit;
}

#undef COMPILE_BACKTRACKINGPATH
#undef CURRENT_AS

void
PRIV(jit_compile)(const REAL_PCRE *re, PUBL(extra) *extra, int mode)
{
struct sljit_compiler *compiler;
backtrack_common rootbacktrack;
compiler_common common_data;
compiler_common *common = &common_data;
const pcre_uint8 *tables = re->tables;
pcre_study_data *study;
int private_data_size;
pcre_uchar *ccend;
executable_functions *functions;
void *executable_func;
sljit_uw executable_size;
struct sljit_label *mainloop = NULL;
struct sljit_label *empty_match_found;
struct sljit_label *empty_match_backtrack;
struct sljit_jump *jump;
struct sljit_jump *reqbyte_notfound = NULL;
struct sljit_jump *empty_match;

SLJIT_ASSERT((extra->flags & PCRE_EXTRA_STUDY_DATA) != 0);
study = extra->study_data;

if (!tables)
  tables = PRIV(default_tables);

memset(&rootbacktrack, 0, sizeof(backtrack_common));
memset(common, 0, sizeof(compiler_common));
rootbacktrack.cc = (pcre_uchar *)re + re->name_table_offset + re->name_count * re->name_entry_size;

common->start = rootbacktrack.cc;
common->fcc = tables + fcc_offset;
common->lcc = (sljit_sw)(tables + lcc_offset);
common->mode = mode;
common->nltype = NLTYPE_FIXED;
switch(re->options & PCRE_NEWLINE_BITS)
  {
  case 0:
  /* Compile-time default */
  switch(NEWLINE)
    {
    case -1: common->newline = (CHAR_CR << 8) | CHAR_NL; common->nltype = NLTYPE_ANY; break;
    case -2: common->newline = (CHAR_CR << 8) | CHAR_NL; common->nltype = NLTYPE_ANYCRLF; break;
    default: common->newline = NEWLINE; break;
    }
  break;
  case PCRE_NEWLINE_CR: common->newline = CHAR_CR; break;
  case PCRE_NEWLINE_LF: common->newline = CHAR_NL; break;
  case PCRE_NEWLINE_CR+
       PCRE_NEWLINE_LF: common->newline = (CHAR_CR << 8) | CHAR_NL; break;
  case PCRE_NEWLINE_ANY: common->newline = (CHAR_CR << 8) | CHAR_NL; common->nltype = NLTYPE_ANY; break;
  case PCRE_NEWLINE_ANYCRLF: common->newline = (CHAR_CR << 8) | CHAR_NL; common->nltype = NLTYPE_ANYCRLF; break;
  default: return;
  }
if ((re->options & PCRE_BSR_ANYCRLF) != 0)
  common->bsr_nltype = NLTYPE_ANYCRLF;
else if ((re->options & PCRE_BSR_UNICODE) != 0)
  common->bsr_nltype = NLTYPE_ANY;
else
  {
#ifdef BSR_ANYCRLF
  common->bsr_nltype = NLTYPE_ANYCRLF;
#else
  common->bsr_nltype = NLTYPE_ANY;
#endif
  }
common->endonly = (re->options & PCRE_DOLLAR_ENDONLY) != 0;
common->ctypes = (sljit_sw)(tables + ctypes_offset);
common->digits[0] = -2;
common->name_table = (sljit_sw)((pcre_uchar *)re + re->name_table_offset);
common->name_count = re->name_count;
common->name_entry_size = re->name_entry_size;
common->jscript_compat = (re->options & PCRE_JAVASCRIPT_COMPAT) != 0;
#ifdef SUPPORT_UTF
/* PCRE_UTF[16|32] have the same value as PCRE_UTF8. */
common->utf = (re->options & PCRE_UTF8) != 0;
#ifdef SUPPORT_UCP
common->use_ucp = (re->options & PCRE_UCP) != 0;
#endif
#endif /* SUPPORT_UTF */
ccend = bracketend(rootbacktrack.cc);

/* Calculate the local space size on the stack. */
common->ovector_start = CALL_LIMIT + sizeof(sljit_sw);
common->optimized_cbracket = (pcre_uint8 *)SLJIT_MALLOC(re->top_bracket + 1);
if (!common->optimized_cbracket)
  return;
memset(common->optimized_cbracket, 1, re->top_bracket + 1);

SLJIT_ASSERT(*rootbacktrack.cc == OP_BRA && ccend[-(1 + LINK_SIZE)] == OP_KET);
private_data_size = get_private_data_length(common, rootbacktrack.cc, ccend);
if (private_data_size < 0)
  {
  SLJIT_FREE(common->optimized_cbracket);
  return;
  }

/* Checking flags and updating ovector_start. */
if (mode == JIT_COMPILE && (re->flags & PCRE_REQCHSET) != 0 && (re->options & PCRE_NO_START_OPTIMIZE) == 0)
  {
  common->req_char_ptr = common->ovector_start;
  common->ovector_start += sizeof(sljit_sw);
  }
if (mode != JIT_COMPILE)
  {
  common->start_used_ptr = common->ovector_start;
  common->ovector_start += sizeof(sljit_sw);
  if (mode == JIT_PARTIAL_SOFT_COMPILE)
    {
    common->hit_start = common->ovector_start;
    common->ovector_start += sizeof(sljit_sw);
    }
  }
if ((re->options & PCRE_FIRSTLINE) != 0)
  {
  common->first_line_end = common->ovector_start;
  common->ovector_start += sizeof(sljit_sw);
  }

/* Aligning ovector to even number of sljit words. */
if ((common->ovector_start & sizeof(sljit_sw)) != 0)
  common->ovector_start += sizeof(sljit_sw);

SLJIT_ASSERT(!(common->req_char_ptr != 0 && common->start_used_ptr != 0));
common->cbraptr = OVECTOR_START + (re->top_bracket + 1) * 2 * sizeof(sljit_sw);
private_data_size += common->cbraptr + (re->top_bracket + 1) * sizeof(sljit_sw);
if (private_data_size > SLJIT_MAX_LOCAL_SIZE)
  {
  SLJIT_FREE(common->optimized_cbracket);
  return;
  }
common->private_data_ptrs = (int *)SLJIT_MALLOC((ccend - rootbacktrack.cc) * sizeof(int));
if (!common->private_data_ptrs)
  {
  SLJIT_FREE(common->optimized_cbracket);
  return;
  }
memset(common->private_data_ptrs, 0, (ccend - rootbacktrack.cc) * sizeof(int));
set_private_data_ptrs(common, common->cbraptr + (re->top_bracket + 1) * sizeof(sljit_sw), ccend);

compiler = sljit_create_compiler();
if (!compiler)
  {
  SLJIT_FREE(common->optimized_cbracket);
  SLJIT_FREE(common->private_data_ptrs);
  return;
  }
common->compiler = compiler;

/* Main pcre_jit_exec entry. */
sljit_emit_enter(compiler, 1, 5, 5, private_data_size);

/* Register init. */
reset_ovector(common, (re->top_bracket + 1) * 2);
if (common->req_char_ptr != 0)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->req_char_ptr, SLJIT_SCRATCH_REG1, 0);

OP1(SLJIT_MOV, ARGUMENTS, 0, SLJIT_SAVED_REG1, 0);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_SAVED_REG1, 0);
OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
OP1(SLJIT_MOV, STR_END, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, end));
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, stack));
OP1(SLJIT_MOV_SI, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, calllimit));
OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(struct sljit_stack, base));
OP1(SLJIT_MOV, STACK_LIMIT, 0, SLJIT_MEM1(TMP2), SLJIT_OFFSETOF(struct sljit_stack, limit));
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), CALL_LIMIT, TMP1, 0);

if (mode == JIT_PARTIAL_SOFT_COMPILE)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, 0);

/* Main part of the matching */
if ((re->options & PCRE_ANCHORED) == 0)
  {
  mainloop = mainloop_entry(common, (re->flags & PCRE_HASCRORLF) != 0, (re->options & PCRE_FIRSTLINE) != 0);
  /* Forward search if possible. */
  if ((re->options & PCRE_NO_START_OPTIMIZE) == 0)
    {
    if (mode == JIT_COMPILE && fast_forward_first_n_chars(common, (re->options & PCRE_FIRSTLINE) != 0))
      { /* Do nothing */ }
    else if ((re->flags & PCRE_FIRSTSET) != 0)
      fast_forward_first_char(common, (pcre_uchar)re->first_char, (re->flags & PCRE_FCH_CASELESS) != 0, (re->options & PCRE_FIRSTLINE) != 0);
    else if ((re->flags & PCRE_STARTLINE) != 0)
      fast_forward_newline(common, (re->options & PCRE_FIRSTLINE) != 0);
    else if ((re->flags & PCRE_STARTLINE) == 0 && study != NULL && (study->flags & PCRE_STUDY_MAPPED) != 0)
      fast_forward_start_bits(common, (sljit_uw)study->start_bits, (re->options & PCRE_FIRSTLINE) != 0);
    }
  }
if (common->req_char_ptr != 0)
  reqbyte_notfound = search_requested_char(common, (pcre_uchar)re->req_char, (re->flags & PCRE_RCH_CASELESS) != 0, (re->flags & PCRE_FIRSTSET) != 0);

/* Store the current STR_PTR in OVECTOR(0). */
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0), STR_PTR, 0);
/* Copy the limit of allowed recursions. */
OP1(SLJIT_MOV, CALL_COUNT, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), CALL_LIMIT);
if (common->mark_ptr != 0)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->mark_ptr, SLJIT_IMM, 0);
/* Copy the beginning of the string. */
if (mode == JIT_PARTIAL_SOFT_COMPILE)
  {
  jump = CMP(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, 0);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);
  JUMPHERE(jump);
  }
else if (mode == JIT_PARTIAL_HARD_COMPILE)
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, STR_PTR, 0);

compile_matchingpath(common, rootbacktrack.cc, ccend, &rootbacktrack);
if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
  {
  sljit_free_compiler(compiler);
  SLJIT_FREE(common->optimized_cbracket);
  SLJIT_FREE(common->private_data_ptrs);
  return;
  }

empty_match = CMP(SLJIT_C_EQUAL, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));
empty_match_found = LABEL();

common->acceptlabel = LABEL();
if (common->accept != NULL)
  set_jumps(common->accept, common->acceptlabel);

/* This means we have a match. Update the ovector. */
copy_ovector(common, re->top_bracket + 1);
common->quitlabel = LABEL();
if (common->quit != NULL)
  set_jumps(common->quit, common->quitlabel);
sljit_emit_return(compiler, SLJIT_MOV, SLJIT_RETURN_REG, 0);

if (mode != JIT_COMPILE)
  {
  common->partialmatchlabel = LABEL();
  set_jumps(common->partialmatch, common->partialmatchlabel);
  return_with_partial_match(common, common->quitlabel);
  }

empty_match_backtrack = LABEL();
compile_backtrackingpath(common, rootbacktrack.top);
if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
  {
  sljit_free_compiler(compiler);
  SLJIT_FREE(common->optimized_cbracket);
  SLJIT_FREE(common->private_data_ptrs);
  return;
  }

SLJIT_ASSERT(rootbacktrack.prev == NULL);

if (mode == JIT_PARTIAL_SOFT_COMPILE)
  {
  /* Update hit_start only in the first time. */
  jump = CMP(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, -1);
  OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->start_used_ptr, SLJIT_IMM, -1);
  OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, TMP1, 0);
  JUMPHERE(jump);
  }

/* Check we have remaining characters. */
OP1(SLJIT_MOV, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), OVECTOR(0));

if ((re->options & PCRE_ANCHORED) == 0)
  {
  if ((re->options & PCRE_FIRSTLINE) == 0)
    {
    if (mode == JIT_COMPILE && study != NULL && study->minlength > 1 && (re->options & PCRE_NO_START_OPTIMIZE) == 0)
      {
      OP2(SLJIT_ADD, TMP1, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(study->minlength + 1));
      CMPTO(SLJIT_C_LESS_EQUAL, TMP1, 0, STR_END, 0, mainloop);
      }
    else
      CMPTO(SLJIT_C_LESS, STR_PTR, 0, STR_END, 0, mainloop);
    }
  else
    {
    SLJIT_ASSERT(common->first_line_end != 0);
    if (mode == JIT_COMPILE && study != NULL && study->minlength > 1 && (re->options & PCRE_NO_START_OPTIMIZE) == 0)
      {
      OP2(SLJIT_ADD, TMP1, 0, STR_PTR, 0, SLJIT_IMM, IN_UCHARS(study->minlength + 1));
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, TMP1, 0, STR_END, 0);
      OP_FLAGS(SLJIT_MOV, TMP2, 0, SLJIT_UNUSED, 0, SLJIT_C_GREATER);
      OP2(SLJIT_SUB | SLJIT_SET_U, SLJIT_UNUSED, 0, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end);
      OP_FLAGS(SLJIT_OR | SLJIT_SET_E, TMP2, 0, TMP2, 0, SLJIT_C_GREATER_EQUAL);
      JUMPTO(SLJIT_C_ZERO, mainloop);
      }
    else
      CMPTO(SLJIT_C_LESS, STR_PTR, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), common->first_line_end, mainloop);
    }
  }

/* No more remaining characters. */
if (reqbyte_notfound != NULL)
  JUMPHERE(reqbyte_notfound);

if (mode == JIT_PARTIAL_SOFT_COMPILE)
  CMPTO(SLJIT_C_NOT_EQUAL, SLJIT_MEM1(SLJIT_LOCALS_REG), common->hit_start, SLJIT_IMM, 0, common->partialmatchlabel);

OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, PCRE_ERROR_NOMATCH);
JUMPTO(SLJIT_JUMP, common->quitlabel);

flush_stubs(common);

JUMPHERE(empty_match);
OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, notempty));
CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, SLJIT_IMM, 0, empty_match_backtrack);
OP1(SLJIT_MOV_UB, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, notempty_atstart));
CMPTO(SLJIT_C_EQUAL, TMP2, 0, SLJIT_IMM, 0, empty_match_found);
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, str));
CMPTO(SLJIT_C_NOT_EQUAL, TMP2, 0, STR_PTR, 0, empty_match_found);
JUMPTO(SLJIT_JUMP, empty_match_backtrack);

common->currententry = common->entries;
while (common->currententry != NULL)
  {
  /* Might add new entries. */
  compile_recurse(common);
  if (SLJIT_UNLIKELY(sljit_get_compiler_error(compiler)))
    {
    sljit_free_compiler(compiler);
    SLJIT_FREE(common->optimized_cbracket);
    SLJIT_FREE(common->private_data_ptrs);
    return;
    }
  flush_stubs(common);
  common->currententry = common->currententry->next;
  }

/* Allocating stack, returns with PCRE_ERROR_JIT_STACKLIMIT if fails. */
/* This is a (really) rare case. */
set_jumps(common->stackalloc, LABEL());
/* RETURN_ADDR is not a saved register. */
sljit_emit_fast_enter(compiler, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);
OP1(SLJIT_MOV, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1, TMP2, 0);
OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, stack));
OP1(SLJIT_MOV, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(struct sljit_stack, top), STACK_TOP, 0);
OP2(SLJIT_ADD, TMP2, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(struct sljit_stack, limit), SLJIT_IMM, STACK_GROWTH_RATE);

sljit_emit_ijump(compiler, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(sljit_stack_resize));
jump = CMP(SLJIT_C_NOT_EQUAL, SLJIT_RETURN_REG, 0, SLJIT_IMM, 0);
OP1(SLJIT_MOV, TMP1, 0, ARGUMENTS, 0);
OP1(SLJIT_MOV, TMP1, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(jit_arguments, stack));
OP1(SLJIT_MOV, STACK_TOP, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(struct sljit_stack, top));
OP1(SLJIT_MOV, STACK_LIMIT, 0, SLJIT_MEM1(TMP1), SLJIT_OFFSETOF(struct sljit_stack, limit));
OP1(SLJIT_MOV, TMP2, 0, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS1);
sljit_emit_fast_return(compiler, SLJIT_MEM1(SLJIT_LOCALS_REG), LOCALS0);

/* Allocation failed. */
JUMPHERE(jump);
/* We break the return address cache here, but this is a really rare case. */
OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, PCRE_ERROR_JIT_STACKLIMIT);
JUMPTO(SLJIT_JUMP, common->quitlabel);

/* Call limit reached. */
set_jumps(common->calllimit, LABEL());
OP1(SLJIT_MOV, SLJIT_RETURN_REG, 0, SLJIT_IMM, PCRE_ERROR_MATCHLIMIT);
JUMPTO(SLJIT_JUMP, common->quitlabel);

if (common->revertframes != NULL)
  {
  set_jumps(common->revertframes, LABEL());
  do_revertframes(common);
  }
if (common->wordboundary != NULL)
  {
  set_jumps(common->wordboundary, LABEL());
  check_wordboundary(common);
  }
if (common->anynewline != NULL)
  {
  set_jumps(common->anynewline, LABEL());
  check_anynewline(common);
  }
if (common->hspace != NULL)
  {
  set_jumps(common->hspace, LABEL());
  check_hspace(common);
  }
if (common->vspace != NULL)
  {
  set_jumps(common->vspace, LABEL());
  check_vspace(common);
  }
if (common->casefulcmp != NULL)
  {
  set_jumps(common->casefulcmp, LABEL());
  do_casefulcmp(common);
  }
if (common->caselesscmp != NULL)
  {
  set_jumps(common->caselesscmp, LABEL());
  do_caselesscmp(common);
  }
#ifdef SUPPORT_UTF
#ifndef COMPILE_PCRE32
if (common->utfreadchar != NULL)
  {
  set_jumps(common->utfreadchar, LABEL());
  do_utfreadchar(common);
  }
#endif /* !COMPILE_PCRE32 */
#ifdef COMPILE_PCRE8
if (common->utfreadtype8 != NULL)
  {
  set_jumps(common->utfreadtype8, LABEL());
  do_utfreadtype8(common);
  }
#endif /* COMPILE_PCRE8 */
#endif /* SUPPORT_UTF */
#ifdef SUPPORT_UCP
if (common->getucd != NULL)
  {
  set_jumps(common->getucd, LABEL());
  do_getucd(common);
  }
#endif

SLJIT_FREE(common->optimized_cbracket);
SLJIT_FREE(common->private_data_ptrs);
executable_func = sljit_generate_code(compiler);
executable_size = sljit_get_generated_code_size(compiler);
sljit_free_compiler(compiler);
if (executable_func == NULL)
  return;

/* Reuse the function descriptor if possible. */
if ((extra->flags & PCRE_EXTRA_EXECUTABLE_JIT) != 0 && extra->executable_jit != NULL)
  functions = (executable_functions *)extra->executable_jit;
else
  {
  /* Note: If your memory-checker has flagged the allocation below as a
   * memory leak, it is probably because you either forgot to call
   * pcre_free_study() (or pcre16_free_study()) on the pcre_extra (or
   * pcre16_extra) object, or you called said function after having
   * cleared the PCRE_EXTRA_EXECUTABLE_JIT bit from the "flags" field
   * of the object. (The function will only free the JIT data if the
   * bit remains set, as the bit indicates that the pointer to the data
   * is valid.)
   */
  functions = SLJIT_MALLOC(sizeof(executable_functions));
  if (functions == NULL)
    {
    /* This case is highly unlikely since we just recently
    freed a lot of memory. Although not impossible. */
    sljit_free_code(executable_func);
    return;
    }
  memset(functions, 0, sizeof(executable_functions));
  functions->top_bracket = (re->top_bracket + 1) * 2;
  extra->executable_jit = functions;
  extra->flags |= PCRE_EXTRA_EXECUTABLE_JIT;
  }

functions->executable_funcs[mode] = executable_func;
functions->executable_sizes[mode] = executable_size;
}

static int jit_machine_stack_exec(jit_arguments *arguments, void* executable_func)
{
union {
   void* executable_func;
   jit_function call_executable_func;
} convert_executable_func;
pcre_uint8 local_space[MACHINE_STACK_SIZE];
struct sljit_stack local_stack;

local_stack.top = (sljit_sw)&local_space;
local_stack.base = local_stack.top;
local_stack.limit = local_stack.base + MACHINE_STACK_SIZE;
local_stack.max_limit = local_stack.limit;
arguments->stack = &local_stack;
convert_executable_func.executable_func = executable_func;
return convert_executable_func.call_executable_func(arguments);
}

int
PRIV(jit_exec)(const PUBL(extra) *extra_data, const pcre_uchar *subject,
  int length, int start_offset, int options, int *offsets, int offsetcount)
{
executable_functions *functions = (executable_functions *)extra_data->executable_jit;
union {
   void* executable_func;
   jit_function call_executable_func;
} convert_executable_func;
jit_arguments arguments;
int maxoffsetcount;
int retval;
int mode = JIT_COMPILE;

if ((options & PCRE_PARTIAL_HARD) != 0)
  mode = JIT_PARTIAL_HARD_COMPILE;
else if ((options & PCRE_PARTIAL_SOFT) != 0)
  mode = JIT_PARTIAL_SOFT_COMPILE;

if (functions->executable_funcs[mode] == NULL)
  return PCRE_ERROR_JIT_BADOPTION;

/* Sanity checks should be handled by pcre_exec. */
arguments.str = subject + start_offset;
arguments.begin = subject;
arguments.end = subject + length;
arguments.mark_ptr = NULL;
/* JIT decreases this value less frequently than the interpreter. */
arguments.calllimit = ((extra_data->flags & PCRE_EXTRA_MATCH_LIMIT) == 0) ? MATCH_LIMIT : extra_data->match_limit;
arguments.notbol = (options & PCRE_NOTBOL) != 0;
arguments.noteol = (options & PCRE_NOTEOL) != 0;
arguments.notempty = (options & PCRE_NOTEMPTY) != 0;
arguments.notempty_atstart = (options & PCRE_NOTEMPTY_ATSTART) != 0;
arguments.offsets = offsets;

/* pcre_exec() rounds offsetcount to a multiple of 3, and then uses only 2/3 of
the output vector for storing captured strings, with the remainder used as
workspace. We don't need the workspace here. For compatibility, we limit the
number of captured strings in the same way as pcre_exec(), so that the user
gets the same result with and without JIT. */

if (offsetcount != 2)
  offsetcount = ((offsetcount - (offsetcount % 3)) * 2) / 3;
maxoffsetcount = functions->top_bracket;
if (offsetcount > maxoffsetcount)
  offsetcount = maxoffsetcount;
arguments.offsetcount = offsetcount;

if (functions->callback)
  arguments.stack = (struct sljit_stack *)functions->callback(functions->userdata);
else
  arguments.stack = (struct sljit_stack *)functions->userdata;

if (arguments.stack == NULL)
  retval = jit_machine_stack_exec(&arguments, functions->executable_funcs[mode]);
else
  {
  convert_executable_func.executable_func = functions->executable_funcs[mode];
  retval = convert_executable_func.call_executable_func(&arguments);
  }

if (retval * 2 > offsetcount)
  retval = 0;
if ((extra_data->flags & PCRE_EXTRA_MARK) != 0)
  *(extra_data->mark) = arguments.mark_ptr;

return retval;
}

#if defined COMPILE_PCRE8
PCRE_EXP_DEFN int PCRE_CALL_CONVENTION
pcre_jit_exec(const pcre *argument_re, const pcre_extra *extra_data,
  PCRE_SPTR subject, int length, int start_offset, int options,
  int *offsets, int offsetcount, pcre_jit_stack *stack)
#elif defined COMPILE_PCRE16
PCRE_EXP_DEFN int PCRE_CALL_CONVENTION
pcre16_jit_exec(const pcre16 *argument_re, const pcre16_extra *extra_data,
  PCRE_SPTR16 subject, int length, int start_offset, int options,
  int *offsets, int offsetcount, pcre16_jit_stack *stack)
#elif defined COMPILE_PCRE32
PCRE_EXP_DEFN int PCRE_CALL_CONVENTION
pcre32_jit_exec(const pcre32 *argument_re, const pcre32_extra *extra_data,
  PCRE_SPTR32 subject, int length, int start_offset, int options,
  int *offsets, int offsetcount, pcre32_jit_stack *stack)
#endif
{
pcre_uchar *subject_ptr = (pcre_uchar *)subject;
executable_functions *functions = (executable_functions *)extra_data->executable_jit;
union {
   void* executable_func;
   jit_function call_executable_func;
} convert_executable_func;
jit_arguments arguments;
int maxoffsetcount;
int retval;
int mode = JIT_COMPILE;

SLJIT_UNUSED_ARG(argument_re);

/* Plausibility checks */
if ((options & ~PUBLIC_JIT_EXEC_OPTIONS) != 0) return PCRE_ERROR_JIT_BADOPTION;

if ((options & PCRE_PARTIAL_HARD) != 0)
  mode = JIT_PARTIAL_HARD_COMPILE;
else if ((options & PCRE_PARTIAL_SOFT) != 0)
  mode = JIT_PARTIAL_SOFT_COMPILE;

if (functions->executable_funcs[mode] == NULL)
  return PCRE_ERROR_JIT_BADOPTION;

/* Sanity checks should be handled by pcre_exec. */
arguments.stack = (struct sljit_stack *)stack;
arguments.str = subject_ptr + start_offset;
arguments.begin = subject_ptr;
arguments.end = subject_ptr + length;
arguments.mark_ptr = NULL;
/* JIT decreases this value less frequently than the interpreter. */
arguments.calllimit = ((extra_data->flags & PCRE_EXTRA_MATCH_LIMIT) == 0) ? MATCH_LIMIT : extra_data->match_limit;
arguments.notbol = (options & PCRE_NOTBOL) != 0;
arguments.noteol = (options & PCRE_NOTEOL) != 0;
arguments.notempty = (options & PCRE_NOTEMPTY) != 0;
arguments.notempty_atstart = (options & PCRE_NOTEMPTY_ATSTART) != 0;
arguments.offsets = offsets;

/* pcre_exec() rounds offsetcount to a multiple of 3, and then uses only 2/3 of
the output vector for storing captured strings, with the remainder used as
workspace. We don't need the workspace here. For compatibility, we limit the
number of captured strings in the same way as pcre_exec(), so that the user
gets the same result with and without JIT. */

if (offsetcount != 2)
  offsetcount = ((offsetcount - (offsetcount % 3)) * 2) / 3;
maxoffsetcount = functions->top_bracket;
if (offsetcount > maxoffsetcount)
  offsetcount = maxoffsetcount;
arguments.offsetcount = offsetcount;

convert_executable_func.executable_func = functions->executable_funcs[mode];
retval = convert_executable_func.call_executable_func(&arguments);

if (retval * 2 > offsetcount)
  retval = 0;
if ((extra_data->flags & PCRE_EXTRA_MARK) != 0)
  *(extra_data->mark) = arguments.mark_ptr;

return retval;
}

void
PRIV(jit_free)(void *executable_funcs)
{
int i;
executable_functions *functions = (executable_functions *)executable_funcs;
for (i = 0; i < JIT_NUMBER_OF_COMPILE_MODES; i++)
  {
  if (functions->executable_funcs[i] != NULL)
    sljit_free_code(functions->executable_funcs[i]);
  }
SLJIT_FREE(functions);
}

int
PRIV(jit_get_size)(void *executable_funcs)
{
int i;
sljit_uw size = 0;
sljit_uw *executable_sizes = ((executable_functions *)executable_funcs)->executable_sizes;
for (i = 0; i < JIT_NUMBER_OF_COMPILE_MODES; i++)
  size += executable_sizes[i];
return (int)size;
}

const char*
PRIV(jit_get_target)(void)
{
return sljit_get_platform_name();
}

#if defined COMPILE_PCRE8
PCRE_EXP_DECL pcre_jit_stack *
pcre_jit_stack_alloc(int startsize, int maxsize)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL pcre16_jit_stack *
pcre16_jit_stack_alloc(int startsize, int maxsize)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL pcre32_jit_stack *
pcre32_jit_stack_alloc(int startsize, int maxsize)
#endif
{
if (startsize < 1 || maxsize < 1)
  return NULL;
if (startsize > maxsize)
  startsize = maxsize;
startsize = (startsize + STACK_GROWTH_RATE - 1) & ~(STACK_GROWTH_RATE - 1);
maxsize = (maxsize + STACK_GROWTH_RATE - 1) & ~(STACK_GROWTH_RATE - 1);
return (PUBL(jit_stack)*)sljit_allocate_stack(startsize, maxsize);
}

#if defined COMPILE_PCRE8
PCRE_EXP_DECL void
pcre_jit_stack_free(pcre_jit_stack *stack)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL void
pcre16_jit_stack_free(pcre16_jit_stack *stack)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL void
pcre32_jit_stack_free(pcre32_jit_stack *stack)
#endif
{
sljit_free_stack((struct sljit_stack *)stack);
}

#if defined COMPILE_PCRE8
PCRE_EXP_DECL void
pcre_assign_jit_stack(pcre_extra *extra, pcre_jit_callback callback, void *userdata)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL void
pcre16_assign_jit_stack(pcre16_extra *extra, pcre16_jit_callback callback, void *userdata)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL void
pcre32_assign_jit_stack(pcre32_extra *extra, pcre32_jit_callback callback, void *userdata)
#endif
{
executable_functions *functions;
if (extra != NULL &&
    (extra->flags & PCRE_EXTRA_EXECUTABLE_JIT) != 0 &&
    extra->executable_jit != NULL)
  {
  functions = (executable_functions *)extra->executable_jit;
  functions->callback = callback;
  functions->userdata = userdata;
  }
}

#else  /* SUPPORT_JIT */

/* These are dummy functions to avoid linking errors when JIT support is not
being compiled. */

#if defined COMPILE_PCRE8
PCRE_EXP_DECL pcre_jit_stack *
pcre_jit_stack_alloc(int startsize, int maxsize)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL pcre16_jit_stack *
pcre16_jit_stack_alloc(int startsize, int maxsize)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL pcre32_jit_stack *
pcre32_jit_stack_alloc(int startsize, int maxsize)
#endif
{
(void)startsize;
(void)maxsize;
return NULL;
}

#if defined COMPILE_PCRE8
PCRE_EXP_DECL void
pcre_jit_stack_free(pcre_jit_stack *stack)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL void
pcre16_jit_stack_free(pcre16_jit_stack *stack)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL void
pcre32_jit_stack_free(pcre32_jit_stack *stack)
#endif
{
(void)stack;
}

#if defined COMPILE_PCRE8
PCRE_EXP_DECL void
pcre_assign_jit_stack(pcre_extra *extra, pcre_jit_callback callback, void *userdata)
#elif defined COMPILE_PCRE16
PCRE_EXP_DECL void
pcre16_assign_jit_stack(pcre16_extra *extra, pcre16_jit_callback callback, void *userdata)
#elif defined COMPILE_PCRE32
PCRE_EXP_DECL void
pcre32_assign_jit_stack(pcre32_extra *extra, pcre32_jit_callback callback, void *userdata)
#endif
{
(void)extra;
(void)callback;
(void)userdata;
}

#endif

/* End of pcre_jit_compile.c */

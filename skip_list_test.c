#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut.h"

#define gc(p) ((struct gc_node *)(p))

int test_skls_creation() {
	struct variable k1, k2, *rv;
	struct skls *ls = skls_new();
	ASSERT_NOTNULL(ls);

	k1.type = T_INT;
	k1.value.i = 1024;
	rv = skls_get(ls, &k1);
	rv->type = T_KSTR;
	rv->value.ref = gc(kstr_new("1024", -1));

	k2.type = T_KSTR;
	k2.value.ref = gc(kstr_new("1024", -1));
	rv = skls_get(ls, &k2);
	rv->type = T_INT;
	rv->value.i = 1024;

	rv = skls_get(ls, &k1);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(rv)->land, "1024");

	rv = skls_get(ls, &k2);
	ASSERT_NOTNULL(rv);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->value.i, 1024LL);
	return 0;
}


TEST_BEGIN
	TEST_ENTRY(skls_creation, normal)
TEST_END

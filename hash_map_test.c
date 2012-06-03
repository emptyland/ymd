#include "value.h"
#include "memory.h"
#include "state.h"
#include "yut_rand.h"
#include "yut.h"

#define gc(p) ((struct gc_node *)(p))

struct hmap *map;
struct hmap *benchmark;

static int test_hmap_creation_1() {
	struct variable k, *rv;

	k.type = T_INT;
	k.value.i = 1024;
	rv = hmap_get(map, &k);
	rv->type = T_KSTR;
	rv->value.ref = gc(kstr_new("1024", -1));

	rv = hmap_get(map, &k);
	ASSERT_EQ(uint, rv->type, T_KSTR);
	ASSERT_STREQ(kstr_of(rv)->land, "1024");

	k.type = T_KSTR;
	k.value.ref = gc(kstr_new("1024", -1));
	rv = hmap_get(map, &k);
	rv->type = T_INT;
	rv->value.i = 1024;

	rv = hmap_get(map, &k);
	ASSERT_EQ(uint, rv->type, T_INT);
	ASSERT_EQ(large, rv->value.i, 1024);
	return 0;
}

#define BENCHMARK_COUNT 100000

static int test_hmap_creation_2() {
	struct variable k, *rv;
	int i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		while (i--) {
			const struct yut_kstr *kz = RAND_STR();
			k.type = T_KSTR;
			k.value.ref = gc(kstr_new(kz->land, kz->len));
			rv = hmap_get(map, &k);
			rv->type = T_INT;
			rv->value.i = i;
		}
	RAND_END
	return 0;
}

static int test_hmap_search() {
	struct variable k, *rv;
	char buf[32];
	int i = BENCHMARK_COUNT;
	while (i--) {
		snprintf(buf, sizeof(buf), "%d", i);
		k.type = T_KSTR;
		k.value.ref = gc(kstr_new(buf, -1));
		rv = hmap_get(map, &k);
		rv->type = T_INT;
		rv->value.i = i;
	}
	i = BENCHMARK_COUNT;
	RAND_BEGIN(NORMAL)
		TIME_RECORD_BEGIN(searching)
		while (i--) {
			unsigned int index = RAND_RANGE(uint, 0, BENCHMARK_COUNT);
			snprintf(buf, sizeof(buf), "%u", index);
			k.type = T_KSTR;
			k.value.ref = gc(kstr_new(buf, -1));
			rv = hmap_get(map, &k);
			ASSERT_EQ(uint, rv->type, T_INT);
			ASSERT_EQ(large, rv->value.i, index);
		}
		TIME_RECORD_END
	RAND_END
	return 0;
}

static int test_hmap_setup() {
	map = hmap_new(-1);
	return 0;
}

TEST_BEGIN_WITH(test_hmap_setup, NULL)
	TEST_ENTRY(hmap_creation_1, normal)
	TEST_ENTRY(hmap_creation_2, benchmark)
	TEST_ENTRY(hmap_search, benchmark)
TEST_END

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include "zsmalloc.h"

#define MIN_SIZE 32
#define MAX_SIZE 4096

void do_zsmalloc_test_all_sizes(struct zs_pool *pool);
void do_zsmalloc_test_min_size(struct zs_pool *pool);
void do_zsmalloc_test_readwrite(struct zs_pool *pool);
void do_zsmalloc_test_span(struct zs_pool *pool);

static const char *teststr = "this is a test, hope it works";

static void do_zsmalloc_test(struct zs_pool *pool)
{
	//do_zsmalloc_test_min_size(pool);
	/*do_zsmalloc_test_all_sizes(pool);*/
	/*do_zsmalloc_test_readwrite(pool);*/
	do_zsmalloc_test_span(pool);
}

/* alloc, map, write, unmap, map, read, compare */
void do_zsmalloc_test_readwrite(struct zs_pool *pool)
{
	void *handle;
	char *buf;
	handle = zs_malloc(pool, 1024);
	if(!handle) {
		pr_err("zs_malloc failed\n");
		return;
	}
	buf = zs_map_object(pool, handle);
	if(!buf) {
		pr_err("write object map failed\n");
		goto free;
	}
	strcpy(buf,teststr);
	zs_unmap_object(pool, handle);

	buf = zs_map_object(pool, handle);
	if(!buf) {
		pr_err("read object map failed\n");
		goto free;
	}
	if(strncmp(buf,teststr,sizeof(teststr)))
		pr_err("readwrite: FAIL\n");
	else
		pr_info("readwrite: PASS");
	zs_unmap_object(pool, handle);
free:
	zs_free(pool, handle);
}

void do_zsmalloc_test_span(struct zs_pool *pool)
{
	//void *handle;
	void *handle[2];
	char *buf;
	int i;

	//handle = NULL;
	handle[0] = NULL;
	handle[1] = NULL;

#if 0
	handle = zs_malloc(pool, 3072);
	if (!handle)
		pr_info("zs_malloc failed\n");
#endif

	for (i=0; i<2; i++) {
		handle[i] = zs_malloc(pool, 3072);
		if(!handle[i]) {
			pr_err("zs_malloc failed\n");
			//goto free;
		}
	}

	/* second alloc spans two pages */
	buf = zs_map_object(pool, handle[1]);
	if(!buf) {
		pr_err("write object map failed\n");
		goto free;
	}
	strcpy(buf,teststr);
	zs_unmap_object(pool, handle[1]);

	buf = zs_map_object(pool, handle[1]);
	if(!buf) {
		pr_err("read object map failed\n");
		goto free;
	}
	if(strncmp(buf,teststr,sizeof(teststr)))
		pr_err("span: FAIL\n");
	else
		pr_info("span: PASS\n");
	zs_unmap_object(pool, handle[1]);

free:
	for (i=0; i<2; i++) {
		if (handle[i])
			zs_free(pool, handle[i]);
	}
}

void do_zsmalloc_test_min_size(struct zs_pool *pool)
{
	int i, num_items = 500;

	void **table = kzalloc(num_items * sizeof(*table), GFP_KERNEL);
	if (!table) {
		pr_info("failed to allocate test table\n");
		return;
	}

	for (i = 0; i < num_items; i++) {
		pr_info("allocating object: %d of size: MIN_SIZE\n", i);
		table[i] = zs_malloc(pool, MIN_SIZE);
	}

	for (i = 0; i < num_items; i++) {
		pr_info("freeing object: %d\n", i);
		zs_free(pool, table[i]);
	}

	kfree(table);
}

void do_zsmalloc_test_all_sizes(struct zs_pool *pool)
{	
	int i;
	void **table;

	table = kzalloc(8192 * sizeof(*table), GFP_KERNEL);
	if (!table) {
		pr_info("failed to allocate test table\n");
		return;
	}

	for (i = 0; i <= MAX_SIZE; i++) {
		table[i] = zs_malloc(pool, i);
	}

	for (i = 0; i <= MAX_SIZE; i++) {
		pr_info("freeing size=%d\n", i);
		if (!table[i])
			pr_info("No object found at index: %d\n", i);
		zs_free(pool, table[i]);
	}

	kfree(table);
}

static int __init zstest_init(void)
{
	struct zs_pool *pool;
	pr_info("zstest init\n");

	pool = zs_create_pool("zstest", GFP_NOIO | __GFP_HIGHMEM);

	do_zsmalloc_test(pool);
	zs_destroy_pool(pool);

	return 0;
}

static void __exit zstest_exit(void)
{
	pr_info("zstest exit\n");
}

module_init(zstest_init);
module_exit(zstest_exit);

MODULE_LICENSE("GPL");

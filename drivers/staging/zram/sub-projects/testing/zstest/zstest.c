/*
 * Testing module for zsmalloc memory alloctor.
 * Partly adopted from Christoph Lameter's SLUB test module (slub_test.c)
 * 
 * Author: Nitin Gupta <ngupta@vflare.org>
 */
#define KMSG_COMPONENT "zstest"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#ifdef CONFIG_ZSTEST_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "zsmalloc.h"

#define TEST_COUNT 10000

#define PARALLEL

#ifdef PARALLEL
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>

struct test_struct {
	struct task_struct *task;
	int cpu;
	int size;
	int count;
	struct zs_pool *pool;
	void **v;
	void (*test_p1)(struct test_struct *);
	void (*test_p2)(struct test_struct *);
	unsigned long start1;
	unsigned long stop1;
	unsigned long start2;
	unsigned long stop2;
} test[NR_CPUS];

static atomic_t tests_running;
static atomic_t phase1_complete;
static DECLARE_COMPLETION(completion1);
static DECLARE_COMPLETION(completion2);
static int started;

/*
 * Allocate TEST_COUNT objects and later free them all again
 */
static void zsmalloc_alloc_then_free_test_p1(struct test_struct *t)
{
	int i;

	for (i = 0; i < t->count; i++)
		t->v[i] = zs_malloc(t->pool, t->size);
}

static void zsmalloc_alloc_then_free_test_p2(struct test_struct *t)
{
	int i;

	for (i = 0; i < t->count; i++)
		zs_free(t->pool, t->v[i]);
}

/*
 * Allocate TEST_COUNT objects. Free them immediately.
 */
static void zsmalloc_alloc_free_test_p1(struct test_struct *t)
{
	int i;

	for (i = 0; i < t->count; i++)
		zs_free(t->pool, zs_malloc(t->pool, t->size));
}

static void zsmalloc_alloc_free_mixed(struct test_struct *t)
{
	unsigned int i = 0, curr_allocs = 0; /* by this thread */

	while (i++ < t->count) {
		int op; /* randomly select alloc or free */
		size_t size; /* Generate size randomly. Ignore t->size.*/

		op = random32() % 2;
		size = random32() % PAGE_SIZE;
		
		if (op) {	/* alloc */
			t->v[curr_allocs] = zs_malloc(t->pool, size);
			if (t->v[curr_allocs])
				curr_allocs++;
		} else {	/* free */
			int idx;
			void *handle;
			
			if (!curr_allocs)
				continue;
			
			idx = random32() % curr_allocs;
			curr_allocs--;
			
			handle = t->v[idx];
			t->v[idx] = t->v[curr_allocs];
			t->v[curr_allocs] = NULL;
			
			zs_free(t->pool, handle);
		}
	}
}

static int thread_fn(void *private)
{
	struct test_struct *t = private;
	cpumask_t newmask = CPU_MASK_NONE;

        cpu_set(t->cpu, newmask);
        set_cpus_allowed(current, newmask);
	t->v = kzalloc(t->count * sizeof(void *), GFP_KERNEL);

	atomic_inc(&tests_running);
	wait_for_completion(&completion1);
	t->start1 = get_cycles();
	t->test_p1(t);
	t->stop1 = get_cycles();
	atomic_inc(&phase1_complete);
	wait_for_completion(&completion2);
	t->start2 = get_cycles();
	if (t->test_p2)
		t->test_p2(t);
	t->stop2 = get_cycles();
	kfree(t->v);
	atomic_dec(&tests_running);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	return 0;
}

/*
 * Concurrently run @p1 followed by concurrent @p2
 * execution. @size specifies the size of objects to
 * allocate (typically by @p1).
 * 
 * Steps:
 *  - Start 1 thread per CPU
 *  - Threads first run @p1 in parallel
 *  - Wait for all threads to finish (phase 1)
 *  - Threads now run @p2 in parallel (@p2 can be NULL)
 *  - Wait for all threads to finish (phase 2)
 */
static void do_concurrent_test(struct zs_pool *pool,
		void (*p1)(struct test_struct *),
		void (*p2)(struct test_struct *),
		int size, const char *name)
{
	int cpu;
	unsigned long time1 = 0;
	unsigned long time2 = 0;
	unsigned long sum1 = 0;
	unsigned long sum2 = 0;

	atomic_set(&tests_running, 0);
	atomic_set(&phase1_complete, 0);
	started = 0;
	init_completion(&completion1);
	init_completion(&completion2);

	for_each_online_cpu(cpu) {
		struct test_struct *t = &test[cpu];

		t->cpu = cpu;
		t->count = TEST_COUNT;
		t->pool = pool;
		t->test_p1 = p1;
		t->test_p2 = p2;
		t->size = size;
		t->task = kthread_run(thread_fn, t, "test%d", cpu);
		if (IS_ERR(t->task)) {
			pr_info("Failed to start test func\n");
			return;
		}
	}

	/* Wait till all processes are running */
	while (atomic_read(&tests_running) < num_online_cpus()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}
	complete_all(&completion1);

	/* Wait till all processes have completed phase 1 */
	while (atomic_read(&phase1_complete) < num_online_cpus()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}
	complete_all(&completion2);

	while (atomic_read(&tests_running)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}

	for_each_online_cpu(cpu)
		kthread_stop(test[cpu].task);

	pr_alert("%s(%d):", name, size);
	for_each_online_cpu(cpu) {
		struct test_struct *t = &test[cpu];

		time1 = t->stop1 - t->start1;
		time2 = t->stop2 - t->start2;
		sum1 += time1;
		sum2 += time2;
		printk(" %d=%lu", cpu, time1 / TEST_COUNT);
		if (p2)
			printk("/%lu", time2 / TEST_COUNT);
	}
	printk(" Average=%lu", sum1 / num_online_cpus() / TEST_COUNT);
	if (p2)
		printk("/%lu", sum2 / num_online_cpus() / TEST_COUNT);
	printk("\n");
	schedule_timeout(200);
}

/* 
 * Allocate in parallel, followed by Free in parallel.
 * Allocations and Frees are not mixed.
 */
static void p_test1(struct zs_pool *pool)
{
	int i;

	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(pool, zsmalloc_alloc_then_free_test_p1,
			zsmalloc_alloc_then_free_test_p2,
			1 << i, "zsmalloc N*alloc N*free");
	}
}

/*
 * Allocate and then free immediately.
 */
static void p_test2(struct zs_pool *pool)
{
	int i;

	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(pool, zsmalloc_alloc_free_test_p1, NULL,
			1 << i, "zsmalloc N*(alloc free)");
	}
}

/*
 * Mix of concurrent allocations and frees
 */
static void p_test3(struct zs_pool *pool)
{
	do_concurrent_test(pool, zsmalloc_alloc_free_mixed,
		zsmalloc_alloc_then_free_test_p2,
		0, "zsmalloc mixed alloc/free");

}
#endif

/*
 * Allocate and free objects such that they span
 * a system page boundary
 */
static void test1(struct zs_pool *pool)
{
	int i;
	void *objs[3];
	const int alloc_sz = 2 * PAGE_SIZE / 3;

	for (i = 0; i < 3; i++) {
		objs[i] = zs_malloc(pool, alloc_sz);
		pr_info("allocated: %p\n", objs[i]);
	}

	for (i = 0; i < 3; i++) {
		zs_free(pool, objs[i]);
		pr_info("freed: %p\n", objs[i]);
		objs[i] = NULL;
	}
}

/*
 * Fist alloc then free
 */
static void test2(struct zs_pool *pool)
{
	size_t size;
	void **table = vzalloc(sizeof(void *) * TEST_COUNT);
	if (!table)
		return;

	for (size = 8; size <= PAGE_SIZE; size <<= 1) {
		int i;
		for (i = 0; i < TEST_COUNT; i++) {
			table[i] = zs_malloc(pool, size);
			if (!table[i])
				pr_info("Failed to allocate for size: %zu\n", size);
		}

		for (i = 0; i < TEST_COUNT; i++) {
			if (!table[i])
				pr_info("No object found at index: %d\n", i);
			zs_free(pool, table[i]);
		}
	}
	
	vfree(table);
}

/*
 * Allocate objects and free immediately
 */
static void test3(struct zs_pool *pool)
{
	size_t size;
	void **table = vzalloc(sizeof(void *) * TEST_COUNT);
	if (!table)
		return;

	for (size = 8; size <= PAGE_SIZE; size <<= 1) {
		int i;
		for (i = 0; i < TEST_COUNT; i++) {
			table[i] = zs_malloc(pool, size);
			if (!table[i])
				pr_info("Failed to allocate for size: %zu\n", size);
			zs_free(pool, table[i]);
		}
	}
	
	vfree(table);
}


static int __init zstest_init(void)
{
	struct zs_pool *pool;
	pr_info("START\n");

	pool = zs_create_pool("zstest", GFP_KERNEL);
	if (!pool)
		return -ENOMEM;

	//test1(pool);
	//test2(pool);
	//test3(pool);
	//p_test1(pool);
	//p_test2(pool);
	p_test3(pool);

	zs_destroy_pool(pool);
	return 0;
}

static void __exit zstest_exit(void)
{
	pr_info("END\n");
}

module_init(zstest_init);
module_exit(zstest_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Testing module for zsmalloc allocator");

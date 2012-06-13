/*  
 *  mymaptest2.c - Mapping two pages contiguously in VA space.
 */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/highmem.h>
#include <asm/tlbflush.h>

int init_module(void)
{
	int i, ret = -ENOMEM;
	struct page *pages[2] = { NULL, NULL };
	unsigned char *buf;

	struct vm_struct *vm;
	pte_t *ptes[2];

	pr_info("MyMapTest2 Begin\n");

	vm = alloc_vm_area(2 * PAGE_SIZE, ptes);
	if (!vm) {
		pr_info("Failed to allocate vm area\n");
		goto out;
	}

	pages[0] = alloc_page(GFP_KERNEL);
	pages[1] = alloc_page(GFP_KERNEL);
	if (!pages[0] || !pages[1]) {
		pr_info("Page allocation failed\n");
		goto out;
	}

	/* Fill pages with test pattern */
	buf = kmap_atomic(pages[0]);
	for (i = 0; i < PAGE_SIZE; i++)
		buf[i] = 'a';
	kunmap_atomic(buf);

	buf = kmap_atomic(pages[1]);
	for (i = 0; i < PAGE_SIZE; i++)
		buf[i] = 'z';
	kunmap_atomic(buf);

	buf = NULL;

#if 0
	/*
	 * Now, map both pages *contiguously* using a different method
	 * and verify contents of each page.
	 */
	ret = map_kernel_range_noflush((unsigned long)vm->addr, 2 * PAGE_SIZE,
				PAGE_KERNEL, pages);
	pr_info("map_kernel_range_noflush returned: %d\n", ret);
#endif

	set_pte(ptes[0], mk_pte(pages[0], PAGE_KERNEL));
	set_pte(ptes[1], mk_pte(pages[1], PAGE_KERNEL));

	buf = vm->addr;

	for (i = 0; i < PAGE_SIZE; i++) {
		if (buf[i] != 'a')
			pr_info("mismatch in page-0 at location %d\n", i);
	}

	for (i = PAGE_SIZE; i <= PAGE_SIZE; i++) {
		if (buf[i] != 'z')
			pr_info("mismatch in page-1 at location %d\n", i);
	}
#if 0
	unmap_kernel_range_noflush((unsigned long)vm->addr, 2 * PAGE_SIZE);
#endif
	set_pte(ptes[0], (pte_t) { 0 });
	set_pte(ptes[1], (pte_t) { 0 });

	__flush_tlb_one((unsigned long)buf);
	__flush_tlb_one((unsigned long)buf + PAGE_SIZE);

	pr_info("Test successful\n");
	ret = 0;	/* Success */
out:
	if (vm)
		free_vm_area(vm);
	if (pages[0])
		__free_page(pages[0]);
	if (pages[1])
		__free_page(pages[1]);

	/*
	 * A non 0 return means init_module failed; module can't be loaded. 
	 */
	return ret;
}

void cleanup_module(void)
{
	pr_info("MyMapTest2 End\n");
}

MODULE_LICENSE("GPL");

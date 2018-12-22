/*
 * base module
 *
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>

extern void clear_all(void);

static int __init setup_base_module_test(void)
{
    clear_all();
	return 0;	/* everything is ok */
}
module_init(setup_base_module_test)

static void __exit exit_base_module_test(void) {
}
module_exit(exit_base_module_test)

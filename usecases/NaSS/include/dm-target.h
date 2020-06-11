/* Made by kanghyun
 * : Device mapper for read-triggering buffer flush ssds
 *
 *
 * 
 *
 */

#ifndef __DM_H__
#define __DM_H__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h> /* For delaying function without sleep */
#include <linux/kernel.h> /* For printk */
#include <linux/slab.h> /* For memory allocation/free */
#include <linux/types.h>
#include <linux/errno.h> /* For error number in debugging */


/* For Device Mapper */

#include <linux/dm-io.h>
#include <linux/device-mapper.h>
#include <linux/dm-kcopyd.h>


/* For block-related operations */
#include <linux/blkdev.h> /* Request-queue related APIs*/
#include <linux/fs.h> /* Includes registeration function with block device */
#include <linux/genhd.h> /* Includes gendisk structure */
#include <linux/bio.h> /* Includes struct bio */


/* Configurable */
//#define __DM_NOTRANS__ /* If set, no actual transaction to target ssd */
//#define __DM_WORKQUEUE__ /* If set, use workqueue (SW opt.) */




/* Debugging functions */
//#define __DM_DEBUG__  
#ifdef __DM_DEBUG__
#define print_dm pr_info
#else
#define print_dm(...) /* Disable dbg functions */
#endif



/* Required Parameters */
#define IO_SIZE 4096 /* Same as page size */
#define MIN_WRITE_WORK_IO 16
#define MIN_READ_WORK_IO 16
#define MIN_IOS 16



/* Required Structures */

struct dm_config {
	struct dm_target *target;
	struct dm_dev *target_ssd; /* Underlying ssd for virtual block device */
	u32 block_size; /* Same as target_ssd's block size */

	u32 sectors_per_block;
	u32 lblocks;
	u32 pblocks;
	u32 num_pblocks;

	/* Performance counters */
	u64 writes;
	u64 reads;
	u64 failed;
	u32 init;
	/* NVRAM structure */
	struct nvram *read_buffer;
	struct nvram *write_buffer;

	/* # of NVRAM entry */ 	
	u32 read_buffer_size;
	u32 write_buffer_size;
 
	/* For SW pipelining */
	
	struct bio_set bs;

	struct workqueue_struct *wq; // Is this necessary?
	struct workqueue_struct *read_wq;
	struct workqueue_struct *write_wq;
	mempool_t *read_workpool;
	mempool_t *write_workpool;

	/* USE down_read(&), up_read(), down_write(), up_write() */
	struct rw_semaphore nvram_write_sema;
	struct rw_semaphore nvram_read_sema;
	
	/* USE spin_lock, spin_unlock */
	spinlock_t lock;

};


struct dm_args {
	struct dm_target *target;
	struct dm_dev *target_ssd;

	u64 data_size; /* Total size of target ssd */
	u32 block_size;
	u32 write_buffer_size; /* Number of ssd's write buffer entry (Each entry is 4KB) */

};

struct write_work {
	struct work_struct worker;
	struct dm_config *dc;
	struct bio *bio;	
	/* More additional necessary components */
};


struct read_work {
	struct work_struct worker;
	struct dm_config *dc;
	struct bio *bio;

};


#endif

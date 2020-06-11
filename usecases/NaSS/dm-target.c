/*
 *
 * Implementation for virtual block device
 *
 *
 */

#include "include/dm-target.h"
#include "include/dm-parse.h"
#include "include/dm-nvram.h"
#include "include/dm-bio.h"


MODULE_LICENSE("GPL");



const int write_cpu_map[] = {2};
const int read_cpu_map[] = {3};


static sector_t get_lbn(struct dm_config *dc, struct bio *bio)
{
	sector_t lbn = bio->bi_iter.bi_sector;
	sector_div(lbn, dc->sectors_per_block);
	return lbn; 
}

static void __do_read(struct dm_config *dc, struct bio *bio)
{



}


/* Wrapper function for __do_read() */
static void do_read(struct work_struct *ws)
{
	struct dm_config *dc;
	char *data;
	struct bio *bio;
	struct read_work *work;

	work = container_of(ws, struct read_work, worker);	
	dc = work->dc;
	bio = work->bio;
	mempool_free(work, dc->read_workpool);
	__do_read(dc, bio);
}




/* No workqueue version */
static int __handle_read(struct dm_config *dc, struct bio *bio)
{
	int ret;
	sector_t lbn;
	int find_index;

	lbn =  get_lbn(dc, bio);
	ret = 0;
	/* Grab a lock */
//	down_read(&dc->nvram_write_sema);
	find_index = nvram_search(dc->write_buffer, lbn, bio);
//	up_read(&dc->nvram_write_sema);

	/* BUG */
	if (find_index == -EINVAL) {
		WARN(1, KERN_WARNING "NVRAM Flush Didn't work!" );
	}

	/* Need to read in target SSD */	
	else if (find_index == -ENODATA) {
		//printk("Here!\n");
		bio_set_dev(bio, dc->target_ssd->bdev);
		generic_make_request(bio);
	}
	
	/* Found in NVRAM, delay applied, data copied. Just return */
	else {
		bio_endio(bio);
	}

	return 0;
}


/* Now, we don't acutally write to ssd*/
static int handle_read(struct dm_config *dc, struct bio *bio)
{
	//TODO
	int ret;
	struct read_work *work;
	struct bio *clone;
	int read_cpu; /* Which CPU to handle read request */

	/* Holds start address of page in each bio */
	char *original_page_addr;
	char *clone_page_addr;

	if (dc->init == 0) bio_endio(bio);

	ret = 0;
//	printk("Read ! Read !");
	/* If incoming request is out-of-range, return immediately */
	/*
	if(bio->bi_iter.bi_sector > dc->pblocks) {
		dc->failed++;
		bio_endio(bio);
		return 0;
	}
	
	else {
	*/
#ifdef __DM_WORKQUEUE__
		/* First make a clone bio */
		clone = alloc_new_bio(dc, bio);
		clone_page_addr = page_address(bio_page(clone));
		original_page_addr = page_address(bio_page(bio));

		memcpy(clone_page_addr, original_page_addr, IO_SIZE);
		/* Put cloned bio into workqueue */
		work = mempool_alloc(dc->read_workpool, GFP_NOIO);
		work->dc = dc;
		work->bio = clone;
		
		read_cpu = read_cpu_map[0];
		INIT_WORK(&(work->worker), do_read);
		queue_work_on(read_cpu, dc->read_wq, &(work->worker));

		/* Return orginal bio */
		bio->bi_status = BLK_STS_OK;
		bio_endio(bio); // XXX: vs bio_zero_endio(bio)?
#else
		/* We don't  need cloned bio in this case */
		ret = __handle_read(dc, bio);
#endif
//	}
	dc->reads++;
	goto out;
out:
	return ret;
}

static void __do_write(struct dm_config *dc, struct bio *bio)
{


}


/* wrapper function for __do_write() */
static void do_write(struct work_struct *ws)
{
	struct dm_config *dc;
	struct bio *bio;
	struct write_work *work;

	work = container_of(ws, struct write_work, worker);
	dc = work->dc;
	bio = work->bio;
	mempool_free(work, dc->write_workpool);
	__do_write(dc, bio);
}


/* No workqueue version */
/* bio is cloned bio*/
static int __handle_write(struct dm_config *dc, struct bio *bio)
{
	int ret;
	sector_t lbn;
	lbn = get_lbn(dc, bio);	

	/* Grab a lock */
//	down_write(&dc->nvram_write_sema);
	ret = nvram_insert(dc->write_buffer, lbn, bio);
//	up_write(&dc->nvram_write_sema);

	dc->writes++;
	return 0;
}


static int handle_write(struct dm_config *dc, struct bio *bio)
{
	int ret;
	struct write_work *work;
	struct bio *clone;
	int write_cpu; /* Which CPU to handle read request */

	/* Holds start address of page in each bio */
	char *original_page_addr;
	char *clone_page_addr;

	ret = 0;
//	print_dm("Write ! Write !");
	/* If incoming request is out-of-range, return immediately */
	/*
	if(bio->bi_iter.bi_sector > dc->pblocks) {
		print_dm("Incoming write failed..");
		dc->failed++;
		bio_endio(bio);
		return 0;
	}
	
	else {
		
	*/
		clone = alloc_new_bio(dc, bio);
		clone_page_addr = page_address(bio_page(clone));
		original_page_addr = page_address(bio_page(bio));

		memcpy(clone_page_addr, original_page_addr, IO_SIZE);

#ifdef __DM_WORKQUEUE__

		/* Put cloned bio into workqueue */
		work = mempool_alloc(dc->write_workpool, GFP_NOIO);
		work->dc = dc;
		work->bio = clone;
		
		write_cpu = write_cpu_map[0];
		INIT_WORK(&(work->worker), do_write);
		queue_work_on(write_cpu, dc->write_wq, &(work->worker));

		/* Return orginal bio */
		bio->bi_status = BLK_STS_OK;
		bio_endio(bio); // XXX: vs bio_zero_endio(bio)?
#else
		/* XXX : order? */
		bio_endio(bio);
		ret = __handle_write(dc, clone);

#endif
//	}
	goto out;
out:
	return ret;

}



static void process_bio(struct dm_config *dc, struct bio *bio)
{
	// TODO
	int ret;
	switch (bio_data_dir(bio)) {
		case READ:
			ret = handle_read(dc, bio);
			break;
		case WRITE:
			ret = handle_write(dc, bio);
	}

	/* When error occurs..*/
	/* ENODEV : no device */
	if (ret < 0) {
		switch (ret) {
			case -EWOULDBLOCK: /* Resource not available, would block */
				bio->bi_status = BLK_STS_AGAIN;
				break;
			case -EINVAL:  /* Invalid Argument */
			case -EIO: /* Input/Output Error */
				bio->bi_status = BLK_STS_IOERR;
				break;
			case -ENODATA: /* No message is available */
				bio->bi_status = BLK_STS_MEDIUM;
				break;
			case -ENOMEM: /* Not enough space to allocate memory */
				bio->bi_status = BLK_STS_RESOURCE;
				break;
			case -EPERM: /* Operation not permitted */
				bio->bi_status = BLK_STS_PROTECTION;
				break;
				}
			dc->failed++;
			bio_endio(bio);
	}

}



static void destroy_args(struct dm_args *da)
{
	// TODO
	if(da->target_ssd) dm_put_device(da->target, da->target_ssd);	

}




/* TODO: Add delay for incoming request */
/* Don't forget to add bio_endio(bio) --> kernel panic */
static int dm_target_map(struct dm_target *target, struct bio *bio)
{
	struct dm_config *dc = target->private;	
	if( dc-> write_buffer_size == 0) {
		bio_set_dev(bio, dc->target_ssd->bdev);
		generic_make_request(bio);
	}
	else process_bio(dc, bio);

	return DM_MAPIO_SUBMITTED;
}



/* TODO: Error handling not implemented */
static int dm_target_ctr(struct dm_target *target, unsigned int argc, char **argv)
{


	struct dm_config *dc;
	struct dm_args da;
	int ret;
		
	sector_t data_size;
	sector_t data_size_temp;
	print_dm("CTR for target started\n");

	memset(&da, 0, sizeof(struct dm_args));
	da.target = target;

	/* Parsing Function */
	ret = parse_argument(&da, argc, argv, &target->error);
	if (ret) {pr_err("Error in parsing\n"); goto parse_out;}


	/* dm_config allocation */
	dc = kzalloc(sizeof(struct dm_config), GFP_NOIO);
	if (dc == NULL) { 
		pr_err("Error in allocation dm_config\n");
		target->error = "Cannot allocate kernel memory";
		ret = -ENOMEM;
		goto dc_alloc_out;
	}

	/* Bug? */
	dc->init = 0;
	/* dm_config setup (Initailization) */
	dc->target = target;
	dc->block_size = da.block_size;
	dc->sectors_per_block = to_sector(da.block_size);
	data_size = target->len;
	data_size_temp = data_size;

	/* logical block size */
	(void)sector_div(data_size, dc->sectors_per_block);
	dc->lblocks = data_size;
	/* physical block size */
	data_size = i_size_read(da.target_ssd->bdev->bd_inode) >> SECTOR_SHIFT;
	(void)sector_div(data_size, dc->sectors_per_block);
	dc->pblocks = data_size;


	dc->target_ssd = da.target_ssd;

	/* NVRAM setting */
	dc->read_buffer = kmalloc(sizeof(struct nvram), GFP_KERNEL);
	dc->write_buffer = kmalloc(sizeof(struct nvram), GFP_KERNEL);

	/* FIXME: configure in parse function */
	dc->read_buffer_size = 2;
	dc->write_buffer_size = da.write_buffer_size;

	ret = nvram_init(dc->read_buffer, dc->read_buffer_size);
	if (ret == -ENOMEM) {
		printk("here called");
		nvram_exit(dc->read_buffer);	
		goto nvram_alloc_out;
	}
	ret = nvram_init(dc->write_buffer, dc->write_buffer_size);
	if (ret == -ENOMEM) {
		nvram_exit(dc->write_buffer);
		goto nvram_alloc_out;
	}
	/* SW pipelining preparing stage */
	
	/* Workqueues, memory pools, bioset */
	
	dc->wq = create_singlethread_workqueue("dm-worker");
	dc->write_wq = create_singlethread_workqueue("dm-write-worker");
	dc->read_wq = create_singlethread_workqueue("dm-read-worker");
	
	if (!dc->wq || !dc->write_wq || !dc->read_wq) goto workqueue_alloc_out;


	/* TODO: Check the size of memory pool below! */
	dc->write_workpool = mempool_create_kmalloc_pool(MIN_WRITE_WORK_IO, sizeof(struct write_work));
	dc->read_workpool = mempool_create_kmalloc_pool(MIN_READ_WORK_IO, sizeof(struct read_work));

	if (!dc->write_workpool || !dc->read_workpool) goto workpool_alloc_out;

	/* May not needed */
//	bioset_init(&dc->bs, MIN_IOS, 0, BIOSET_NEED_BVECS);

	/* Semaphores */
	init_rwsem(&dc->nvram_write_sema);
	init_rwsem(&dc->nvram_read_sema);

	spin_lock_init(&dc->lock);

	/* Performance counter*/
	dc->writes = 0;
	dc->reads = 0;
	dc->failed = 0;
	

	target->private = dc;
	print_dm("CTR for target finished\n");
	if(!dc->read_buffer || !dc->write_buffer) printk("??");


	dc->init = 1;
	return 0;

workpool_alloc_out:
	flush_workqueue(dc->wq);
	destroy_workqueue(dc->wq);
	flush_workqueue(dc->read_wq);
	destroy_workqueue(dc->read_wq);
	flush_workqueue(dc->write_wq);
	destroy_workqueue(dc->write_wq);
workqueue_alloc_out:
nvram_alloc_out:
	kfree(dc);
dc_alloc_out:
	destroy_args(&da);
parse_out:
	print_dm("CTR for target finished with some errors\n");
	return ret;
}


static void dm_target_dtr(struct dm_target *target)
{
	//FIXME
	struct dm_config *dc = target->private;
	
	print_dm("DTR for target started\n");

	if(!dc) print_dm("pointer to dc is NULL");

	/* free nvram */
	nvram_exit(dc->read_buffer);
	nvram_exit(dc->write_buffer);
	
	/* free workqueue */
	flush_workqueue(dc->wq);
	destroy_workqueue(dc->wq);

	flush_workqueue(dc->read_wq);
	destroy_workqueue(dc->read_wq);

	flush_workqueue(dc->write_wq);
	destroy_workqueue(dc->write_wq);

	/* free mempool */
	mempool_destroy(dc->read_workpool);
	mempool_destroy(dc->write_workpool);

	dm_put_device(target, dc->target_ssd);
	kfree(dc);

	print_dm("DTR or target finished\n");
	return ;
}


static void dm_target_status (struct dm_target *target, status_type_t type, 
       			unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct dm_config *dc = target->private;
	int sz = 0;
	switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("\n%s %llu %s %llu %s %llu\n",
				"Write_num:", dc->writes, "Read_num:", dc->reads, "Failed_num:", dc->failed);
			DMEMIT("%s %u %s %u\n", 
				"NVRAM hit num:", dc->write_buffer->hit, "NVRAM miss num:", dc->write_buffer->miss); 
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("%s", dc->target_ssd->name);		
	}
	return ;	

}

static struct target_type dm_read_trigger_target = {

	.name = "dm-read-trigger",
	.version = {1,0,0},
	.module = THIS_MODULE,
	.ctr = dm_target_ctr,
	.dtr = dm_target_dtr,
	.map = dm_target_map,
	.status = dm_target_status
};


static int __init dm_init(void)
{

	printk("DM target for Read Triggering SSD has started\n");
	return dm_register_target(&dm_read_trigger_target);

}


static void __exit dm_exit(void)
{

	printk("DM exit for Read Triggering SSD has started\n");
	dm_unregister_target(&dm_read_trigger_target);

}



module_init(dm_init);
module_exit(dm_exit);


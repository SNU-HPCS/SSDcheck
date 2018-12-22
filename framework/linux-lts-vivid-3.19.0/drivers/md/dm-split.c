/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "split"

/*
 * Split: maps a split range of a device.
 */
struct split_c {
	struct dm_dev *dev;
    sector_t start; // start address given from dmsetup (per device offset)
    uint32_t bitidx1;
    uint32_t bitidx1_val;
    uint32_t bitidx2;
    uint32_t bitidx2_val;
};

static void dump_split_c(struct split_c *sc)
{
    printk(KERN_INFO "<dump_split_c> sc->start:%llu\n", (uint64_t)sc->start);
    printk(KERN_INFO "<dump_split_c> sc->bitidx1:%d (%d)\n",sc->bitidx1, sc->bitidx1_val);
    printk(KERN_INFO "<dump_split_c> sc->bitidx2:%d (%d)\n",sc->bitidx2, sc->bitidx2_val);
}

/*
 * Construct a split mapping: <dev_path> <offset>
 */
static int split_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct split_c *sc;
	unsigned long long tmp;
    int tmp2;
	char dummy;

	if (argc != 4) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	sc = kmalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		ti->error = "dm-split: Cannot allocate split context";
		return -ENOMEM;
	}

	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1) {
		ti->error = "dm-split: Invalid device sector";
		goto bad;
	}
	sc->start = tmp;

	if (sscanf(argv[2], "%llu:%d%c", &tmp, &tmp2, &dummy) != 2) {
		ti->error = "dm-split: Invalid bitidx1 & bitidx1_val";
		goto bad;
	}
    sc->bitidx1 = tmp; 
    sc->bitidx1_val = tmp2;

	if (sscanf(argv[3], "%llu:%d%c", &tmp, &tmp2, &dummy) != 2) {
		ti->error = "dm-split: Invalid bitidx2 & bitidx2_val";
		goto bad;
	}
    sc->bitidx2 = tmp;
    sc->bitidx2_val = tmp2;

    if ((sc->bitidx1 == 0) ||
            (sc->bitidx2 != 0 && sc->bitidx2 != sc->bitidx1 + 1)) {
		ti->error = "dm-split: Invalid format of bitidx1 & bitidx2";
		goto bad;
    }

	if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &sc->dev)) {
		ti->error = "dm-split: Device lookup failed";
		goto bad;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_same_bios = 1;
	ti->private = sc;
	return 0;

      bad:
	kfree(sc);
	return -EINVAL;
}

static void split_dtr(struct dm_target *ti)
{
	struct split_c *sc = (struct split_c *) ti->private;

	dm_put_device(ti, sc->dev);
	kfree(sc);
}

static int address_validity_check(struct split_c *sc, sector_t target_sector)
{
    uint64_t target_address = target_sector << 9;

    if (sc->bitidx1) {
        if (sc->bitidx1_val != ((target_address & (1 << sc->bitidx1)) >> sc->bitidx1))
            return -1;
    }

    if (sc->bitidx2) {
        if (sc->bitidx2_val != ((target_address & (1 << sc->bitidx2)) >> sc->bitidx2))
            return -1;
    }
    return 0;
}

static sector_t split_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct split_c *sc = ti->private;
    sector_t dm_offset = dm_target_offset(ti, bi_sector);
    sector_t target_sector = sc->start + dm_offset;
    sector_t new_sector;

    if (sc->bitidx2) { // double bit
        uint64_t bitidx1_mask = (1 << (sc->bitidx1 - 9)) - 1; // sector based translation
        /*uint64_t bitidx2_mask = (1 << (sc->bitidx2 - 9)) - 1; // sector based translation*/
        sector_t lower_sector = target_sector & bitidx1_mask;
        sector_t upper_sector = (target_sector & (~bitidx1_mask)) << 2;
        new_sector = upper_sector | 
            lower_sector | 
            (sc->bitidx1_val << (sc->bitidx1 - 9)) |
            (sc->bitidx2_val << (sc->bitidx2 - 9));
    } else { // single bit
        uint64_t bitidx1_mask = (1 << (sc->bitidx1 - 9)) - 1; // sector based translation
        sector_t lower_sector = target_sector & bitidx1_mask;
        sector_t upper_sector = (target_sector & (~bitidx1_mask)) << 1;
        new_sector = upper_sector | 
            lower_sector | 
            (sc->bitidx1_val << (sc->bitidx1 - 9));
    }

    if (unlikely(address_validity_check(sc, new_sector))) {
        printk(KERN_INFO "<split_map_sector> [ERROR] ori_sector:%10llx, new_sector:%10llx\n\n",
                (uint64_t)target_sector, (uint64_t)new_sector);
        dump_split_c(sc);
    }
    return new_sector;

	/*return sc->start + dm_target_offset(ti, bi_sector);*/
}

// remap block device
static void split_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct split_c *sc = ti->private;

    /*printk(KERN_INFO "<split_map_bio> bio->bi_iter.bi_sector:%llu\n", (unsigned long long)bio->bi_iter.bi_sector);*/
    /*printk(KERN_INFO "<split_map_bio> bio->bi_iter.bi_size:%d\n",bio->bi_iter.bi_size);*/

	bio->bi_bdev = sc->dev->bdev;
	if (bio_sectors(bio))
		bio->bi_iter.bi_sector =
			split_map_sector(ti, bio->bi_iter.bi_sector);
}

static int split_map(struct dm_target *ti, struct bio *bio)
{
	split_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static void split_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
{
	struct split_c *sc = (struct split_c *) ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %llu", sc->dev->name,
				(unsigned long long)sc->start);
		/*snprintf(result, maxlen, "%s %d %d", sc->dev->name,*/
				/*sc->bitidx1,*/
				/*sc->bitidx2);*/
		break;
	}
}

static int split_ioctl(struct dm_target *ti, unsigned int cmd,
			unsigned long arg)
{
	struct split_c *sc = (struct split_c *) ti->private;
	struct dm_dev *dev = sc->dev;
	int r = 0;

    /*printk(KERN_INFO "<split_ioctl> ti->begin:%d\n",(int)ti->begin); // from first*/
    /*printk(KERN_INFO "<split_ioctl> ti->len:%d\n",(int)ti->len);*/
    /*printk(KERN_INFO "<split_ioctl> ti->max_io_len:%d\n",ti->max_io_len);*/
    /*dump_split_c(sc);*/

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (sc->start ||
	    ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT)
		r = scsi_verify_blk_ioctl(NULL, cmd);

	return r ? : __blkdev_driver_ioctl(dev->bdev, dev->mode, cmd, arg);
}

static int split_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct split_c *sc = ti->private;
	struct request_queue *q = bdev_get_queue(sc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = sc->dev->bdev;
	bvm->bi_sector = split_map_sector(ti, bvm->bi_sector);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int split_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct split_c *sc = ti->private;

	return fn(ti, sc->dev, sc->start, ti->len, data);
}

static struct target_type split_target = {
	.name   = "split",
	.version = {1, 2, 1},
	.module = THIS_MODULE,
	.ctr    = split_ctr,
	.dtr    = split_dtr,
	.map    = split_map,
	.status = split_status,
	.ioctl  = split_ioctl,
	.merge  = split_merge,
	.iterate_devices = split_iterate_devices,
};

int __init dm_split_init(void)
{
    int rc;

    printk(KERN_INFO "[dm_split] insmod\n");
	rc = dm_register_target(&split_target);
    printk(KERN_INFO "[dm_split] dm_register_target ==> rc:%d\n", rc);

	if (rc < 0)
		DMERR("register failed %d", rc);

	return rc;
}

void dm_split_exit(void)
{
	dm_unregister_target(&split_target);
    printk(KERN_INFO "[dm_split] unregister target\n");
}

module_init(dm_split_init)
module_exit(dm_split_exit);


MODULE_AUTHOR("HPCS, SNU");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("split dm");

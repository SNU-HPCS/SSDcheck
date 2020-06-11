#include "include/dm-parse.h"
#include "include/dm-target.h"


#define MIN_DATA_DEV_BLOCK_SIZE (4 * 1024)
#define MAX_DATA_DEV_BLOCK_SIZE (1024 * 1024)
#define __DMDEDUP_DEBUG__

/* Parsing Functions */


/* Parsing Function for underlying ssd */

static int parse_target_ssd(struct dm_args *da, struct dm_arg_set *as, char **err)
{
	int ret;
	const char *path;
	path = dm_shift_arg(as);
	ret = dm_get_device(da->target, path, dm_table_get_mode(da->target->table), &da->target_ssd);
	if (ret) {
		*err = "Error opening target ssd";
	}
	
	else {
	     da->data_size = i_size_read(da->target_ssd->bdev->bd_inode); //TODO: Check
	}
	return ret;
}

/* Parsing Function for block size of virtual block device */
static int parse_block_size(struct dm_args *da, struct dm_arg_set *as, char **err)
{
	u32 block_size;
	if(kstrtou32(dm_shift_arg(as), 10, &block_size) || !block_size || block_size < MIN_DATA_DEV_BLOCK_SIZE || block_size > MAX_DATA_DEV_BLOCK_SIZE || !is_power_of_2(block_size)) {
		*err = "Invalid data block size";
		return -EINVAL;
	}

	if(block_size > da->data_size) {
		print_dm("Block Size : %d, Data Device Size : %lld:", block_size, da->data_size);		
		*err = "Data block size is larger than the data device";
		return -EINVAL;
	}

	
	da->block_size = block_size;
	return 0;
}


static int parse_write_buffer_size(struct dm_args *da, struct dm_arg_set *as, char **err)
{
	u32 write_buffer_size;
	kstrtou32(dm_shift_arg(as), 10, &write_buffer_size);
	if (write_buffer_size < 0 ) { *err = "Invalid buffer_size"; return -EINVAL;}
	da->write_buffer_size = write_buffer_size;
	return 0; 
}


/* Wrapper function for further parsing functions */
/* Argument is, target_ssd, block_size, read_delay, write_delay */
int parse_argument(struct dm_args *da, unsigned int argc, char **argv, char **err)
{
	struct dm_arg_set as;	
	int ret;
	int index;
	print_dm("Total Index # : %d\n", argc);
	for (index = 0; index < argc; index++) {
		print_dm("Index #: %d, Argument : %s", index, argv[index]); 
	}

	if (argc < 3) {
		*err = "Insufficient args";
		return -EINVAL;
	}

	if (argc > 3) { 
		*err = "Too many args";
		return -EINVAL;
	}

	as.argc = argc;
	as.argv = argv;

	ret = parse_target_ssd(da, &as, err);
	if (ret) return ret;
	ret = parse_block_size(da, &as, err);
	if (ret) return ret;

	ret = parse_write_buffer_size(da, &as, err);
	if (ret) return ret;

	// TODO
	return 0;
}


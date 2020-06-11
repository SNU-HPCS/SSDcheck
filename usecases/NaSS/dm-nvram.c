#include "include/dm-nvram.h"


/* Delay with nvram_delay */
static void nvram_delay(struct nvram *nvram)
{
	udelay(nvram->nvram_delay);
	return;
}


/* Flush all bios in write buffer in NVRAM */
static int nvram_flush(struct nvram *nvram)
{
	int buffer_index;
	int total_index;

	struct nvram_buffer *data;	
	struct nvram_buffer *start_data;

	buffer_index = 0;	
	total_index = nvram->total_size;
	start_data = nvram->data;

	print_dm("Now flushing nvram...");

	if (nvram->current_size != nvram->total_size) {
		WARN(1, KERN_WARNING "NVRAM flush should not be invoked here..");
		return -EINVAL;
	}
	
	/* Flush all bios */
	for (buffer_index = 0; buffer_index < total_index; buffer_index ++) {
		data = 	start_data + buffer_index;
		generic_make_request(data->bio);
		data->lbn = 0;
		data->buffered_data = NULL;
		data->bio = NULL; /* ? */	
	}

	
	nvram->current_size = 0;
	return 0;
}



/* Returns -ENODATA when miss, else returns index */
/* Returns -EINVAL when flush didn't work..*/
int nvram_search(struct nvram *nvram, sector_t lbn, struct bio *bio)
{
	int find_index;
	int search_index;
	int current_buffer_size;
	
	/* Stored data in nvram which we want to find */
	struct nvram_buffer *data;
	struct nvram_buffer *start_data;

	search_index = 0;
	find_index = -ENODATA;
	current_buffer_size = nvram->current_size;
	start_data = nvram->data;	

	/* Flush didn't work*/
	if (current_buffer_size == nvram->total_size) {
		return -EINVAL;
	}	

	/* Search in reverse order */
	for (search_index = (current_buffer_size-1) ; search_index >=0 ;  search_index--) {
		data = start_data + search_index;
		if (data->lbn == lbn) {
			find_index = search_index;
			break;
		}
	}

//	print_dm("Now reading in nvram..");
//	print_dm("Search_index : %d", search_index);

	/* FIXME : Need to delay in here too? */	
	if (find_index == -ENODATA) {		
		nvram->miss++;
	}


	else {
		memcpy(page_address(bio_page(bio)), data->buffered_data, 4096);
		nvram_delay(nvram);
		nvram->hit++;
	}

	return find_index;
}



/* Called in write function */
int nvram_insert(struct nvram * nvram, sector_t lbn, struct bio *bio)
{
	int insert_index;
	int current_buffer_size;
	struct nvram_buffer *data;

	current_buffer_size = nvram->current_size;
	insert_index = current_buffer_size;

	print_dm("Now writing into nvram..");
	/* Address to insert new entry */
	data = nvram->data + current_buffer_size; 	
	data->bio = bio;
	data->lbn = lbn;
	data->buffered_data = page_address(bio_page(bio)); 	

	nvram->current_size++;
	if (nvram->current_size == nvram->total_size) {
		nvram_flush(nvram);
	}

	return 0;
}



/* Called in ctr, dtr */
int nvram_init(struct nvram *nvram, u32 size)
{

//	nvram = kzalloc(sizeof(struct nvram), GFP_KERNEL);
	if (!nvram) {
		print_dm("nvram allocation failed");
		goto kzalloc_out;
	}	
	nvram->total_size = size;
	nvram->data = kmalloc(sizeof(char) * 4096 * size, GFP_KERNEL);
	if (!nvram->data) {
		print_dm("nvram buffer allocation failed");
		goto vzalloc_out;
	}
	nvram->current_size = 0;
	nvram->nvram_delay = 200; /* in us*/
	nvram->hit = 0;
	nvram->miss = 0;

	/* Circular buffer is not implemented yet */
	nvram->head = 0;
	nvram->tail = 0;
	print_dm("nvram_init done!");
	return 0;

vzalloc_out:
	kfree(nvram);
kzalloc_out:
	return -ENOMEM;
}


void nvram_exit(struct nvram *nvram)
{
	if(nvram->total_size !=0) kfree(nvram->data);
	kfree(nvram);
}

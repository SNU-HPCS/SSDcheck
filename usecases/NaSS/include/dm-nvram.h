#ifndef __DM_NVRAM__
#define __DM_NVRAM__

#include "dm-target.h"


/* Do we need real device? */
/* Or just emulate this with some delay? */
/* Making too many clones might degrade overall latency.. */

struct nvram_buffer {
	struct bio *bio;
	sector_t lbn; // holds destination address
	char *buffered_data; // 4K granularity.
//	u32 time; /* request time? */
};


struct nvram {
/* May Need in flush operation */
//	struct device dev;
	u32 total_size;
	struct nvram_buffer *data;
	u32 current_size;

	/* in us */
	u32 nvram_delay; 	

	/* NVRAM hit, miss ratio */
	u32 hit;
	u32 miss;
		
	/* Make it circular?*/
	u32 head;
	u32 tail;

};


/* Flush is only for write_nvram ? */

// int nvram_flush (struct nvram *);

int nvram_search (struct nvram *, sector_t lbn, struct bio *bio);
int nvram_insert (struct nvram *, sector_t lbn, struct bio *bio);
int nvram_init (struct nvram *, u32 );
void nvram_exit(struct nvram *);

#endif

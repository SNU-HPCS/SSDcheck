#include "include/dm-bio.h"



static void my_endio(struct bio *bio)
{
	struct dm_config *dc;
	unsigned rw = bio_data_dir(bio);
//	struct bio_vec *bvec;
	dc = bio->bi_private;
	
	if (rw == READ || rw == WRITE || rw == REQ_OP_WRITE) {
		// This needed? 
		bio_free_pages(bio);
	}	

	bio_put(bio);
}



struct bio *alloc_new_bio(struct dm_config *dc, struct bio *orig_bio)
{
	struct bio *clone;
	struct page *page;
	clone = bio_kmalloc(GFP_NOIO, 1);
	
	if (!clone) goto out;
	
	bio_set_dev(clone, dc->target_ssd->bdev);
	clone->bi_opf = orig_bio->bi_opf;
	clone->bi_private = dc;
	clone->bi_iter.bi_sector = orig_bio->bi_iter.bi_sector;
	clone->bi_end_io = my_endio;

	/* Can use GFP_KERNEL? */
	page = alloc_pages(GFP_NOIO,0);
 	
	if (!page) 
		goto bad_putbio;
	if (!bio_add_page(clone, page, IO_SIZE, 0))
		goto bad_freepage;


	goto out;

bad_putbio:
	free_pages((unsigned long) page_address(page), 0);
bad_freepage:
	bio_put(clone);
	clone = NULL;
out : 
	return clone;

} 

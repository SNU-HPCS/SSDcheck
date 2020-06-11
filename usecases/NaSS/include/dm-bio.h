#ifndef __DM_BIO__
#define __DM_BIO__

#include "dm-target.h"


struct bio* alloc_new_bio(struct dm_config *, struct bio *);


#endif

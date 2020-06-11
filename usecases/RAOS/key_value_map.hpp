#ifndef USECASE_OBJECT_STORAGE_KEY_VALUE_MAP_HPP
#define USECASE_OBJECT_STORAGE_KEY_VALUE_MAP_HPP

#include <stdbool.h>
#include <stdint.h>

typedef struct _data_loc_t {
	int num_replica;
	int *ssd_ids;
	uint64_t *offsets;
} data_loc_t;

typedef struct _value_ctx_t {
	bool is_valid;
	data_loc_t *dataloc;
} value_context_t;

#endif //USECASE_OBJECT_STORAGE_KEY_VALUE_MAP_HPP

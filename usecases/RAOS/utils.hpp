#ifndef USECASE_OBJECT_STORAGE_UTILS_HPP
#define USECASE_OBJECT_STORAGE_UTILS_HPP

#include <list>
#define TOTAL_SSD_NUM 3

typedef enum {
	EXEC_MODE_NORMAL = 0,
	EXEC_MODE_IDLEAWARE,
} EXEC_MODE_T;

typedef struct _arg_ctx_t {
	int th_num;
	int block_size;
	int read_ratio; // 100 => all read, 0 => all write
	int thinktime;
	int num_replica;
	int tot_req_count;
	char *log_fname;

	EXEC_MODE_T exec_mode;
}arg_ctx_t;

const char *get_ssd_devname(int ssd_id);
int arg_parser(int argc, char *argv[], arg_ctx_t *arg_ctx);
void print_cdf(std::list<double> *lat_list);

#endif //USECASE_OBJECT_STORAGE_UTILS_HPP

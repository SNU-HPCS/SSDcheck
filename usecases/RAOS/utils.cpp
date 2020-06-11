#include <stdio.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <list>
#include "utils.hpp"

using namespace std;

const char *get_ssd_devname(int ssd_id) {
	if (ssd_id >= TOTAL_SSD_NUM || ssd_id < 0)
		assert(0);

//  For debbugin in the local environment
//	switch (ssd_id) {
//		case 0:
//			return "./raw_file_1";
//		case 1:
//			return "./raw_file_2";
//		case 2:
//			return "./raw_file_3";
//		default:
//			break;
//	}

	switch (ssd_id) {
		case 0:
			return "/dev/sdb";
		case 1:
			return "/dev/sdc";
		case 2:
			return "/dev/sdd";
		default:
			break;
	}
	assert(0);
}

void usage(int argc, char *argv[]) {
	printf("Usage %s <th_num> <block_size(ex: 4k, 4m)> <read_ratio> <thinktime(us)> <num_replica> <tot_req_count> <EXEC_MODE> <log_name>\n", argv[0]);
	printf("Example %s 32 4m 50 5 2 100 0", argv[0]);
}

int arg_parser(int argc, char *argv[], arg_ctx_t *arg_ctx) {
	if (argc != 9) {
		usage(argc, argv);
		return -1;
	}

	arg_ctx->th_num = atoi(argv[1]);
	if (argv[2][strlen(argv[2]) - 1] == 'k' || argv[2][strlen(argv[2]) - 1] == 'K') {
		argv[2][strlen(argv[2]) - 1] = '\0';
		arg_ctx->block_size = atoi(argv[2]) * 1024;
	} else if (argv[2][strlen(argv[2]) - 1] == 'm' || argv[2][strlen(argv[2]) - 1] == 'M') {
		argv[2][strlen(argv[2]) - 1] = '\0';
		arg_ctx->block_size = atoi(argv[2]) * 1024 * 1024;
	}
	arg_ctx->read_ratio = atoi(argv[3]);
	arg_ctx->thinktime = atoi(argv[4]);
	arg_ctx->num_replica = atoi(argv[5]);
	arg_ctx->tot_req_count = atoi(argv[6]);
	switch(atoi(argv[7])) {
		case 0:
			arg_ctx->exec_mode = EXEC_MODE_NORMAL;
			break;
		case 1:
			arg_ctx->exec_mode = EXEC_MODE_IDLEAWARE;
			break;
		default:
			assert(0);
	}
	arg_ctx->log_fname = argv[8];
	return 0;
}


void print_cdf(std::list<double> *lat_list) {
    int list_size = lat_list->size();
    list<double>::iterator iter;
    lat_list->sort();

//    for (list<double>::iterator iter = lat_list->begin(); iter != lat_list->end(); iter++) {
//        printf("%lf ", *iter);
//    }

    iter = lat_list->begin(); std::advance(iter, (int)((double)list_size * 0.5) );
    printf("50%%   :%lf\n", *iter);

    iter = lat_list->begin(); std::advance(iter, (int)((double)list_size * 0.99) );
    printf("99%%   :%lf\n", *iter);

    iter = lat_list->begin(); std::advance(iter, (int)((double)list_size * 0.995) );
    printf("99.5%% :%lf\n", *iter);

    iter = lat_list->begin(); std::advance(iter, (int)((double)list_size * 0.999) );
    printf("99.9%% :%lf\n", *iter);
//
//    list<int> aa;
//// ...
//
//    list<int>::iterator iter = aa.begin();
//    std::advance(iter, 123);
//
//    cout<< *iter <<endl;
}
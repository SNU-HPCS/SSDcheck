/*	This code makes read/write traces for fio
	write_num means the number of write requests between read request, 
	iter_num means how many times to write (write, read) pairs in tracefile.

*/

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<time.h>


int main(int argc, char **argv)
{
	srand(time(NULL));
	/* argv[0] is write number between read */
	FILE *fp;
//	fp = fopen("test.log", "w+t");
	fp = fopen(argv[3], "w+t");
	int i, iter;
	//char *p1, p2;
	int write_num = atoi(argv[1]);
	int iter_num = atoi(argv[2]);

//	printf("write_num : %d\n", write_num);
//	printf("iter_num : %d\n", iter_num);

	fprintf(fp, "fio version 2 iolog\n");
	fprintf(fp, "/dev/dm-0 add\n");
	fprintf(fp, "/dev/dm-0 open\n");

	
	for (iter = 0; iter< iter_num; iter++) {
		for (i = 0; i < write_num; i++) {
			fprintf(fp, "/dev/sdb write %d 4096\n", (rand()/4096)*4096); 
		}
		fprintf(fp, "/dev/sdb read %d 4096\n", (rand()/4096)*4096);
		fprintf(fp, "/dev/sdb read %d 4096\n", (rand()/4096)*4096);
//		fprintf(fp, "/dev/dm-0 read %d 4096\n", (rand()/4096)*4096);
//		fprintf(fp, "/dev/dm-0 read %d 4096\n", (rand()/4096)*4096);

	}

	fprintf(fp, "/dev/dm-0 close\n");
	fclose(fp);
	return 0;

	
}

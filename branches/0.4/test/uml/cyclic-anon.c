#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	char *ptr;
	int size = -1;
	int loop, i;
	if (argc > 1) {
		size = atoi(argv[1]);
	}

	if (size <= 0) {
		printf("no size specified\n");
		return 0;
	}

	printf("Size: %dMB\n", size);
	size *= 1024*1024;

	ptr = (char*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (!ptr) {
		perror("mmap error");
		exit(-1);
	}
	for(loop=0;;loop++) {
		printf("loop %d\n", loop);
		mlockall(MCL_CURRENT);
		sleep(10);
	}
	munmap(ptr, size);
	return 0;
}

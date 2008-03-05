#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "ddsetup.h"

int main(int argc, char *argv[])
{
	struct ddresult result;
	int dd, dm, len, i, mode = DMREAD|DMWRITE;
	char text[100], *prog = argv[0], *name = argv[1];

	if (argc < 4) {
		printf("usage: %s <name> <sectors> <type> args...\n", prog);
		return 1;
	}

	if ((dm = open("/dev/mapper/control", O_RDONLY)) == -1)
		goto whoops;

	if ((dd = ioctl(dm, DDLINK)) == -1)
		goto whoops;

	if (ioctl(dd, DMTABLE, &(struct ddtable){ .targets = 1, .mode = mode }) == -1)
		goto report;

	for (i = 3; i < argc; i++)
		if (write(dd, argv[i], strlen(argv[i])) == -1)
			goto report;

	if (ioctl(dd, DMTARGET, &(struct ddtarget){ .sectors = atoi(argv[2]) }) == -1)
		goto report;

	if (write(dd, name, strlen(name)) == -1)
		goto report;

	if (ioctl(dd, DMCREATE) == -1)
		goto report;

	if ((len = read(dd, &result, sizeof(result))) == -1)
		goto report;

	if (len != sizeof result && (errno = EINVAL))
		goto whoops;

	snprintf(text, sizeof(text), "/dev/mapper/%s", name);
	if (unlink(text) == -1 && errno != ENOENT)
		goto remove;

	if (mknod(text, S_IFBLK, makedev(result.dev.major, result.dev.minor)) == -1)
		goto remove;
	return 0;

report:
	if ((len = read(dd, text, sizeof(text))) == -1 || !len)
		goto whoops;
	printf("%s: %s (%.*s)\n", prog, strerror(errno), len, text);
	return 1;

remove:
	ioctl(dd, DMREMOVE, name);
whoops:
	printf("%s: %s (%i)\n", prog, strerror(errno), errno);
	return 1;
}

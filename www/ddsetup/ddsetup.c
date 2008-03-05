#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ddsetup.h"

// to do:
//  - spinlocks in base ddlink ops
//  - uuids and major/minor command forms
//  - more dmsetup commands

struct token { char *text; int size; };

int get_number(struct token *token, long long *result)
{
	char *next;
	long long number = strtoll(token->text, &next, 10);
	if (next - token->text == token->size) {
		*result = number;
		return 1;
	}
	return 0;
}

int table_line(int dd, struct token *token, int tokens)
{
	long long offset, sectors;
	struct ddtarget target;
	int i;

	if (tokens < 3 ||
		!get_number(&token[0], &offset) ||
		!get_number(&token[1], &sectors))
		return -1;

	if (dd < 0)
		return 0; /* only checking syntax */

	for (i = 2; i < tokens; i++)
		if (write(dd, token[i].text, token[i].size) == -1)
			return -1;

	target.offset = offset;
	target.sectors = sectors;
	if ((ioctl(dd, DMTARGET, &target)) == -1)
		return -1;
	return 0;
}

void set_dmname(char *buffer, unsigned size, char *name)
{
	snprintf(buffer, size , "%s%s", "/dev/mapper/", name);
}

int do_create(int dd, char *name, struct token *tokenvec, unsigned *linevec, int lines)
{
	int line, start, devnum;
	char filename[100];
	struct ddresult got;
	int mode = DMREAD|DMWRITE;

	if (ioctl(dd, DMTABLE, &(struct ddtable){ .targets = lines, .mode = mode }) == -1)
		return -1;

	for (start = 0, line = 0; line < lines; start = linevec[line++])
		if (table_line(dd, tokenvec + start, linevec[line] - start) == -1)
			return -1;

	if (write(dd, name, strlen(name)) == -1)
		return -2;
	if (ioctl(dd, DMCREATE) == -1)
		return -2;
	if (read(dd, &got, sizeof(got)) == -1)
		return -2;

	devnum = makedev(got.dev.major, got.dev.minor);
	set_dmname(filename, sizeof(filename), name);
	return mknod(filename, S_IFBLK, devnum);
}

int do_remove(int dd, char *name)
{
	char filename[100];
	if (write(dd, name, strlen(name)) == -1)
		return -2;
	if ((ioctl(dd, DMREMOVE)) == -1)
		return -2;
	set_dmname(filename, sizeof(filename), name);
	if (unlink(filename) == -1)
		return -2;
	return 0;
}

char *plural(int n)
{
	return n > 1 ? "s:" : n ? ":" : "";
}

char *flagged(int f)
{
	return f? "y" : "n";
}

int main(int argc, char *argv[])
{
	char *action = argv[1];
	int dm, dd, len, num;

	if (argc == 1)
		goto usage;
	if ((dm = open("/dev/mapper/control", O_RDWR)) == -1)
		goto whoops;
	if ((dd = ioctl(dm, DDLINK)) == -1)
		goto whoops;
	if (!strcmp(action, "create")) {
		if (argc < 3)
			goto usage;

		struct stat stat;
		if ((fstat(0, &stat)) == -1)
			return -1;

		if (S_ISCHR(stat.st_mode)) {
			printf("No table given\n");
			exit(1);
		}

		char text[2 << 12];
		int maxtokens = 1000, maxlines = 100;
		int tokens = 0, lines = 0;
		struct token tokenvec[maxtokens];
		unsigned linevec[maxlines]; // handle overflow!!!
		unsigned len = read(0, text, sizeof(text));
		char *next = text, *end = text + len;

		while(tokens < maxtokens) {
			while (next < end && !isgraph(*next) && *next != '\n')
				next++;
			if (next == end || *next == '\n') {
				linevec[lines++] = tokens;
				if (next == end || ++next == end)
					break;
				continue;
			}
			char *last = tokenvec[tokens].text = next ;
			while (next < end && isgraph(*next))
				next++;
			tokenvec[tokens].size = next - last;
			tokens++;
		}
		int line, start;
		for (start = 0, line = 0; line < lines; start = linevec[line++])
			if (table_line(-1, tokenvec + start, linevec[line] - start) < 0) {
				printf("input table syntax error, line %i\n", line + 1);
				exit(1);
			}
		if (do_create(dd, argv[2], tokenvec, linevec, lines))
			goto whoops;
		return 0;
	}
	if (!strcmp(action, "remove")) {
		if (argc < 3)
			goto usage;
		switch (do_remove(dd, argv[2])) {
			case 0: break;
			case -2: goto report;
			default: goto whoops;
		}
		return 0;
	}
	if (!strcmp(action, "suspend")) {
		if (argc < 3)
			goto usage;
		if (write(dd, argv[2], strlen(argv[2])) == -1)
			goto report;
		if ((ioctl(dd, DMSUSPEND)) == -1)
			goto report;
		return 0;
	}
	if (!strcmp(action, "resume")) {
		if (argc < 3)
			goto usage;
		if (write(dd, argv[2], strlen(argv[2])) == -1)
			goto report;
		if ((ioctl(dd, DMRESUME)) == -1)
			goto report;
		return 0;
	}
	if (!strcmp(action, "rename")) {
		if (argc != 4)
			goto usage;
		if (write(dd, argv[2], strlen(argv[2])) == -1)
			goto report;
		if (write(dd, argv[3], strlen(argv[3])) == -1)
			goto report;
		if ((ioctl(dd, DMRENAME)) == -1)
			goto report;
		return 0;
	}
	if (!strcmp(action, "ls")) {
		struct { struct ddname name; char space[100]; } got;
		if (argc != 2)
			goto usage;
		if ((ioctl(dd, DMNAMES)) == -1)
			goto report;
		while ((len = read(dd, &got, sizeof(got)))) {
			if (len == -1)
				goto whoops;
			printf("%.*s (%i.%i)\n",
				len - sizeof got.name,
				got.name.name,
				got.name.dev.major,
				got.name.dev.minor);
		}
		return 0;
	}
	if (!strcmp(action, "deps")) {
		struct devnum got;
		if (argc != 3)
			goto usage;
		if (write(dd, argv[2], strlen(argv[2])) == -1)
			goto report;
		if ((num = ioctl(dd, DMDEPS)) == -1)
			goto report;
		printf("%s has %i target%s", argv[2], num, plural(num));
		while ((len = read(dd, &got, sizeof(got)))) {
			if (len == -1)
				goto report;
			printf(" (%u.%u)", got.major, got.minor);
		}
		printf("\n");
		return 0;
	}
	if (!strcmp(action, "info")) {
		struct dmstatus got;
		if (argc != 3)
			goto usage;
		if (write(dd, argv[2], strlen(argv[2])) == -1)
			goto report;
		if ((ioctl(dd, DMSTATUS)) == -1)
			goto report;
		if ((len = read(dd, &got, sizeof(got))) == -1)
			goto report;
		if (len != sizeof(got) && (errno = ENODATA))
			goto whoops;
		printf("%s: (%u.%u) ", argv[2], got.dev.major, got.dev.minor);
		printf("targets %i, ", got.targets);
		printf("opens %i, ", got.opens);
		printf("event %i, ", got.event);
		printf("present=%s, ", flagged(got.flags & DMFLAG_PRESENT));
		printf("readonly=%s, ", flagged(got.flags & DMFLAG_READONLY));
		printf("suspended=%s\n", flagged(got.flags & DMFLAG_SUSPEND));
		while ((len = read(dd, &got, sizeof(got)))) {
			if (len == -1)
				goto report;
			printf(" (%u.%u)", got.dev.major, got.dev.minor);
		}
		return 0;
	}
	if (!strcmp(action, "version")) {
		struct ddversion got;
		if (argc != 2)
			goto usage;
		if ((ioctl(dd, DMVERSION)) == -1)
			goto report;
		if ((len = read(dd, &got, sizeof(got))) == -1)
			goto report;
		if (len != sizeof(got) && (errno = ENODATA))
			goto whoops;
		printf("ddsetup version %i.%i.%i\n", got.major, got.minor, got.point);
		return 0;
	}
	if (!strcmp(action, "targets")) {
		struct { struct ddtype type; char space[100]; } got;
		if (argc != 2)
			goto usage;
		if ((ioctl(dd, DMTYPES)) == -1)
			goto report;
		while ((len = read(dd, &got, sizeof(got)))) {
			if (len == -1)
				goto report;
			printf("%-18.*s v%i.%i.%i\n",
				len - sizeof(struct ddtype), got.type.name,
				got.type.version.major,
				got.type.version.minor,
				got.type.version.point);
		}
		return 0;
	}
usage:
	printf("usage: %s <command> [<device>] [<options>]\n", argv[0]);
	exit(1);
report:;
	char text[100];
	if ((len = read(dd, text, sizeof(text))) == -1 || !len)
		goto whoops;
	printf("%s: %s (%.*s)\n", argv[0], strerror(errno), len, text);
	exit(1);
whoops:
	printf("%s: %s (%i)\n", argv[0], strerror(errno), errno);
	return 1;
	exit(1);
}

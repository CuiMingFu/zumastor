/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *  FILE(s)     : fs_perms.c simpletest.sh textx.o Makefile README
 *  DESCRIPTION : Regression test for Linux filesystem permissions.
 *  AUTHOR      : Jeff Martin (martinjn@us.ibm.com)
 *  HISTORY     :
 *     (04/12/01)v.99  First attempt at using C for fs-regression test.  Only tests read and write bits.
 *     (04/19/01)v1.0  Added test for execute bit.
 *     (05/23/01)v1.1  Added command line parameter to specify test file.
 *     (07/12/01)v1.2  Removed conf file and went to command line parameters.
 *
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <tet_api.h>

void startup();
void fperm_test();
void cleaner();

void (*tet_startup)() = startup, (*tet_cleanup)() = cleaner;
struct tet_testlist tet_testlist[] = { { fperm_test, 1 }, { NULL, 0 } };

void testsetup(mode_t mode, int cuserId, int cgroupId);
int testfperm(int userId, int groupId, char *fperm);

char origwd[128];
char *targfs;

struct {
	mode_t	mode;
	int cuserId;
	int cgroupId;
	int userId;
	int groupId;
	char *fperm;
	int exresult;
} params[18] = {
	{ 001, 99, 99, 12, 100, "x", 1 },
	{ 010, 99, 99, 200, 99, "x", 1 },
	{ 100, 99, 99, 99, 500, "x", 1 },
	{ 002, 99, 99, 12, 100, "w", 1 },
	{ 020, 99, 99, 200, 99, "w", 1 },
	{ 200, 99, 99, 99, 500, "w", 1 },
	{ 004, 99, 99, 12, 100, "r", 1 },
	{ 040, 99, 99, 200, 99, "r", 1 },
	{ 400, 99, 99, 99, 500, "r", 1 },
	{ 000, 99, 99, 99, 99, "r", 0 },
	{ 000, 99, 99, 99, 99, "w", 0 },
	{ 000, 99, 99, 99, 99, "x", 0 },
	{ 010, 99, 99, 99, 500, "x", 0 },
	{ 100, 99, 99, 200, 99, "x", 0 },
	{ 020, 99, 99, 99, 500, "w", 0 },
	{ 200, 99, 99, 200, 99, "w", 0 },
	{ 040, 99, 99, 99, 500, "r", 0 },
	{ 400, 99, 99, 200, 99, "r", 0 }
};

void
fperm_test()
{
	int result;
	int i;
	for (i = 0; i < 18; i++) {
		testsetup(params[i].mode, params[i].cuserId, params[i].cgroupId);
		result = testfperm(params[i].userId, params[i].groupId, params[i].fperm);
		system("rm test.file");
		tet_printf("%c a %03o file owned by (%d/%d) as user/group(%d/%d)  ",
			params[i].fperm, params[i].mode, params[i].cuserId,
			params[i].cgroupId, params[i].userId, params[i].groupId);
		if (result == params[i].exresult) {
			tet_printf("Iteration %d passed.\n", i);
		}
		else {
			tet_printf("Iteration %d failed.\n", i);
			break;
		}
	}
	if (i == 18)
		tet_result(TET_PASS);
	else
		tet_result(TET_FAIL);
}

void
testsetup(mode_t mode, int cuserId, int cgroupId)
{
	char buf[128];

	sprintf(buf, "cp %s/testx.file test.file", origwd);
	system(buf);
	chmod("test.file", mode);
	chown("test.file", cuserId, cgroupId);
}

int
testfperm(int userId, int groupId, char *fperm)
{
	FILE *testfile;
	pid_t PID;
	int tmpi, nuthertmpi;

	/*  SET CURRENT USER/GROUP PERMISSIONS */
	if(setegid(groupId)) {
		tet_printf("could not setegid to %d.\n",groupId);
		seteuid(0);
		setegid(0);
		return(-1);
	}
	if(seteuid(userId)) {
		tet_printf("could not seteuid to %d.\n",userId);
		seteuid(0);
		setegid(0);
		return(-1);
	}

	switch(tolower(fperm[0])) {
		case 'x':
			PID = fork();
			if (PID == 0) {
				execlp("./test.file","test.file",NULL);
				exit(0);
			}
			wait(&tmpi);
			nuthertmpi=WEXITSTATUS(tmpi);
			seteuid(0);
			setegid(0);
			return(nuthertmpi);

		default:
			if((testfile=fopen("test.file",fperm))){
				fclose(testfile);
				seteuid(0);
				setegid(0);
				return (1);
			}
			else {
				seteuid(0);
				setegid(0);
				return (0);
			}
	}
}

void
startup()
{
	targfs = tet_getvar("TEST_TARGET_DIR");
	if (targfs == NULL || *targfs == '\0') {
		tet_infoline("Configuration variable TEST_TARGET_DIR unset!");
		tet_result(TET_UNRESOLVED);
		return;
	}
	if (getcwd(origwd, sizeof(origwd)) == NULL) {
		tet_infoline("Call to getcwd failed!");
		tet_result(TET_UNRESOLVED);
		return;
	}
	chdir(targfs);
}

void
cleaner()
{
	unlink("test.file");
	chdir(origwd);
}

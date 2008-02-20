/**
 * This is just a sample of how unit tests might work.
 */

#define _GNU_SOURCE // for O_DIRECT
#include <test/test.h>

#define main my_main

#include "ddsnap.c"

#undef main

#include <sys/wait.h>

void test_noargs(void)
{
	char* args[] = { "ddsnap" };
	const pid_t pid = fork();
	if (pid == 0) {
		my_main(1, args);
	} else {
		int status;
		ASSERT_TRUE(pid == wait(&status));
		ASSERT_TRUE(WIFEXITED(status) == TRUE);
		ASSERT_TRUE(WEXITSTATUS(status) == 1);
	}
}

void test_help(void)
{
	char* usage_flags[] = { "--usage", "-?", "--help" };
	int i;
	for (i = 0; i < (sizeof(usage_flags)/sizeof(char*)); ++i) {
		char* args[] = { "ddsnap", usage_flags[i] };
		const pid_t pid = fork();
		if (pid == 0) {
			my_main(2, args);
		} else {
			int status;
			ASSERT_TRUE(pid == wait(&status));
			ASSERT_TRUE(WIFEXITED(status) == TRUE);
			ASSERT_TRUE(WEXITSTATUS(status) == 0);
		}
	}
}

test_suite get_suite(void)
{
	return MAKE_SIMPLE_SUITE("testddsnap",
							 SIMPLE_TEST(test_noargs),
							 SIMPLE_TEST(test_help));
}

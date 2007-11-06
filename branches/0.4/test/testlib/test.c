#include <test/test.h>
#include "testrunner.h"

#include <stdio.h>

void do_nothing(void)
{
}

void release_suite(test_suite suite)
{
	free(suite.tests);
}

void test_assert(BOOLEAN test,
				 const char* description,
				 const char* file,
				 unsigned int line)
{
	testrunner_assert(test, description, file, line);
}

void test_fail(const char* description, const char* file, unsigned int line)
{
	testrunner_fail(description, file, line);
}

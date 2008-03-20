#include <test/test.h>
#include <stdio.h>

void test_simple(void)
{
	ASSERT_TRUE((1+1) == 2);
}

void test2(void)
{
	ASSERT_TRUE((2+2) == 4);
}

void test_three_way(void)
{
	int a, b, c;
	a = rand();
	b = a;
	c = b;
	ASSERT_TRUE(a == b);
	ASSERT_TRUE(a == c);
	ASSERT_TRUE(b == c);
}

void test_failure(void)
{
	int a, b;
	a = rand();
	b = rand();
	ASSERT_TRUE(a == b);
}

void setup_failure(void)
{
	ASSERT_TRUE(0);
}

void test_setup_failure(void)
{
	ASSERT_TRUE(1);
}

void tear_down(void)
{
	printf("Tear down\n");
}

test_suite get_suite(void)
{
	return MAKE_SIMPLE_SUITE("simple suite",
							 SIMPLE_TEST(test_simple),
							 SIMPLE_TEST(test2),
							 SIMPLE_TEST(test_failure),
							 SIMPLE_TEST(test_three_way),
							 TEST_CASE(test_setup_failure, setup_failure, tear_down));
}

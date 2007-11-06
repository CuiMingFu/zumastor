#ifndef	TEST_H_
#define	TEST_H_
#include <stdlib.h>
#include <string.h>

typedef enum { TRUE = 1, FALSE = 0 } BOOLEAN;

/**
 * Dummy function that literally does nothing. This is a place holder
 * in the event that setup and teardown functions aren't needed for a
 * particular unit test.
 */

void do_nothing(void);

typedef struct _test test;
struct _test {
  const char* name;
  void (*setup)(void);
  void (*teardown)(void);
  void (*test_case)(void);
};

#define TEST_CASE(x, test_setup, test_teardown) {#x, test_setup, test_teardown, x}
#define SIMPLE_TEST(x) TEST_CASE(x, do_nothing, do_nothing)

typedef struct _test_suite {
  const char* name;
  void (*setup)(void);
  void (*teardown)(void);
  unsigned int num_tests;
  test* tests;
} test_suite;

/* Deep down, don't you just *love* macros? */
#define MAKE_SUITE(aName, aSetup, aTeardown, someTests...) \
  ({test __tests[] = { someTests }; \
    test_suite __suite = { aName, aSetup, aTeardown, sizeof(__tests)/sizeof(test), NULL }; \
    __suite.tests = (test*) malloc (sizeof(__tests)); \
    memcpy(__suite.tests, __tests, sizeof(__tests)); \
    __suite; \
  })

#define MAKE_SIMPLE_SUITE(aName, someTests...) MAKE_SUITE(aName, do_nothing, do_nothing, someTests)

/**
 * Free up resources allocated to a suite.
 *
 * @param aSuite the suite to release
 */

void release_suite(test_suite aSuite);

/**
 * Enforce an assertion.
 *
 * @param test TRUE if the assertion is true, otherwise FALSE
 * @param description a description of the assertion
 * @param file the source file containing the assertion
 * @param line the line number of the source file containing the assertion
 */

void test_assert(BOOLEAN test, const char* description, const char* file, unsigned int line);

/**
 * Enforce a failure. Failures are basically assertions that always fail.
 *
 * @param description a description of the assertion
 * @param file the source file containing the assertion
 * @param line the line number of the source file containing the assertion
 * @see #test_assert()
 */

void test_fail(const char* description, const char* file, unsigned int line);

#define ASSERT_TRUE(x) test_assert(x, "(" #x ")", __FILE__, __LINE__)
#define ASSERT_WITH_MESSAGE(x) test_assert(x, y, __FILE__, __LINE__)
#define FAIL(x) test_fail(x, __FILE__, __LINE__);


/**
 * Define the suite of tests for this program. This function must be
 * defined exactly once for each test executable created for unit
 * tests. This method is invoked by the test_runner.
 *
 * @return the suite to test
 */

test_suite get_suite(void);

#endif 	    /* !TEST_H_ */

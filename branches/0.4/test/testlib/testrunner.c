#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <test/test.h>
#include "testrunner.h"
#include <setjmp.h>

/**
 * Prints out the usage for invoking the test runner..
 *
 * @param program_name the name of the program to be displayed in the usage output
 */

void printUsage(const char* program_name)
{
	printf("Usage: %s [OPTION] [TESTNAME]\n", program_name);
	printf("Run all tests, or just test TESTNAME\n");
	printf("\n\t-f fork mode: fork for each test");
	printf("\n\t-s single process mode: run all tests in one process");
	printf("\n\t-v verbose mode");
	printf("\n\t-h print out this help\n");
}

typedef enum { OK = 0, USAGE = 3, TEST_INIT, TEST_CLEANUP, TEST_NOT_FOUND } return_code;
typedef enum { PASS = 0, FAIL, UNRESOLVED, INVALID } test_outcome;
static const char* TEST_OUTCOMES[] = { "PASS", "FAIL", "UNRESOLVED" };

typedef struct _results results;
struct _results {
	unsigned int run;
	unsigned int total_assertions;
	unsigned int passed;
	unsigned int failed;
	unsigned int unresolved;
};

typedef enum { SETUP = 1, TEST, TEARDOWN } stage;
static const char* STAGE_NAMES[] = { "SETUP", "TEST", "TEARDOWN" };
static stage current_stage;
static volatile results* test_results;
static sigjmp_buf recovery_buffer;
static BOOLEAN verbose_mode = FALSE;
static test_outcome (*test_runner)(const test_suite suite, const test* test_case);
static void (*fallback_function)(void);
static const char* current_suite = NULL;
static const char* current_test = NULL;

/**
 * Print out the results of the test run.
 */

void printResults(void)
{
	printf("Test run: %u\nTests passed: %u\nAssertions passed: %u\nTests failed: %u\nTests unresolved: %u\n",
		   test_results->run,
		   test_results->passed,
		   test_results->total_assertions,
		   test_results->failed,
		   test_results->unresolved);
}

/**
 * Test runner's enforcement of an assertion. This should trigger a
 * failure of some kind if the assertion proves to be false. The
 * function will use the context provided in its arguments (as well as
 * information about which test is running) to provide useful
 * diagnostic messages in the event of a failure.
 *
 * @param test the outcome of the test which is being asserted
 * @param description the description of the assertion
 * @param file the source file where the assertion is being made
 * @param line the line in the source file where the assertion is being made
 */

void testrunner_assert(BOOLEAN test,
					   const char* description,
					   const char* file,
					   unsigned int line)
{
	if (!test) {
		printf("Assertion %s failed in %s stage of %s:%s at %s:%u\n",
			   description,
			   STAGE_NAMES[current_stage],
			   current_suite,
			   current_test,
			   file,
			   line);
		fallback_function();
	} else {
		++(test_results->total_assertions);
	}
}

/**
 * Test runner's enforcement of a failure. Failures are effectively
 * assertions that always fail.
 *
 * @param description the description of the assertion
 * @param file the source file where the assertion is being made
 * @param line the line in the source file where the assertion is being made
 * @see #testrunner_assert()
 */

void testrunner_fail(const char* description,
					 const char* file,
					 unsigned int line)
{
	printf("Failure %s in %s:%s at line %s:%u\n",
		   description,
		   current_suite,
		   current_test,
		   file,
		   line);
	fallback_function();
}

/**
 * Initialize the test environment.
 *
 * @return TRUE if the environment was successfully initialized
 */

BOOLEAN init_testing(void)
{
	results* temp_results = mmap(0,
								 sizeof(results),
								 PROT_READ|PROT_WRITE,
								 MAP_SHARED|MAP_ANONYMOUS,
								 -1,
								 0);

	if (((results*) - 1) == temp_results) {
		fprintf(stderr, "Unable to map shared memory for test results\n");
		test_results = NULL;
		return FALSE;
	}

	memset(temp_results, 0, sizeof(temp_results));
	test_results = temp_results;
	return TRUE;
}

/**
 * Clean up the testing environment.
 *
 * @return TRUE if the cleanup was successful
 */

BOOLEAN cleanup_testing(void)
{
	if (NULL != test_results) {
		if (-1 == munmap((results*)test_results, sizeof(results))) {
			perror("Error cleaning up memory");
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * Monitor a test running a child process. Observe if it fails to run completely.
 *
 * @param pid the child's process id
 * @return the outcome of the test
 */

test_outcome monitor_test(pid_t pid)
{
	int status;
WAITING:
	if (-1 == waitpid(pid, &status, 0)) {
		const char* FORMAT = "Failure while waiting for %s:%s to complete";
		char* buffer = alloca(strlen(FORMAT) + strlen(current_suite) + strlen(current_test) - 3);
		sprintf(buffer, FORMAT, current_suite, current_test);
		perror(buffer);
		cleanup_testing();
		return UNRESOLVED;
	}

	if (WIFEXITED(status)) {
		int exit_code = WEXITSTATUS(status);
		if (exit_code < INVALID) {
			//			fprintf(stderr, "%s\n", TEST_OUTCOMES[exit_code]);
			return exit_code;
		} else {
			fprintf(stderr, "%s:%s had invalid exit code: %d\n", current_suite, current_test, exit_code);
			abort(); /* This should also never happen */
		}
	} else if (WIFSIGNALED(status)) {
		printf("%s:%s interrupted by signal\n", current_suite, current_test);
		++(test_results->unresolved);
		return UNRESOLVED;
	} else if (WIFSTOPPED(status)) {
		printf("%s:%s suspended by signal\n", current_suite, current_test);
		goto WAITING;
	} else if (WIFCONTINUED(status)) {
		printf("%s:%s resumed by signal", current_suite, current_test);
		goto WAITING;
	}

	return UNRESOLVED; /* not reachable */
}

/**
 * Fallback by exiting with the appropriate return code. This is the
 * normal fallback function.
 */

static void exit_fallback(void)
{
	exit((current_stage == TEST) ? FAIL : UNRESOLVED);
}

/**
 * Fallback by calling siglongjmp(). This is clearly evil, but helpful
 * when it comes to debugging unit tests. Should be used in
 * conjunction with #run_test_with_siglongjmp().
 */

static void siglongjmp_fallback(void)
{
	siglongjmp(recovery_buffer, current_stage);
}

#define LOG_TEST_ERROR(x,...) fprintf(stderr, x, current_suite, current_test, ##__VA_ARGS__)
#define VERBOSE_LOG(format,...) if (verbose_mode) printf(format, current_suite, current_test, ##__VA_ARGS__)

/**
 * Run a test case.
 *
 * @param test_case the test case
 * @param suite_name the name of the test suite which contains this test
 * @return PASS if there no assertion failures, FAIL if there is an assertion failure while running the test, UNRESOLVED otherwise.
 */

static test_outcome run_test(const test_suite suite, const test* test_case)
{
    VERBOSE_LOG("Test %s:%s running in child process %u\n", getpid());
    current_stage = SETUP;
	VERBOSE_LOG("Setting up suite for test %s:%s\n");
	suite.setup();
    VERBOSE_LOG("Setting up test %s:%s\n");
    test_case->setup();
    VERBOSE_LOG("Running %s:%s\n");
    current_stage = TEST;
    test_case->test_case();
    current_stage = TEARDOWN;
    VERBOSE_LOG("Running %s:%s tear down\n");
    test_case->teardown();
	VERBOSE_LOG("Tearing down suite for %s:%s\n");
    return PASS;
}

/**
 * Run a test case in a child process.
 *
 * @param test_case the test case
 * @param suite_name the name of the test suite which contains this test
 * @return PASS if there are no assertion failures, FAIL if there is an assertion failure while running the test, UNRESOLVED otherwise.
 * @see #run_test()
 */

static test_outcome run_test_in_child(const test_suite suite, const test* test_case)
{
	const pid_t pid = fork();
	if (pid) {
		test_outcome outcome = monitor_test(pid);
		if (outcome >= INVALID) {
			fprintf(stderr, "Invalid test outcome for %s:%s\n", current_suite, current_test);
			abort(); /* this should never happen, and if it does, I want a core dump */
		} 
		return outcome;
	} else {
		exit(run_test(suite, test_case));
	}
}

/**
 * Run tests using siglongjmp() to fallback in the event of a
 * failure. Yes, this is evil, but it is usefull when you are
 * debugging your tests. This should be used in conjunction with
 * #siglongjmp_fallback().
 *
 * @param suite the suite containing the test
 * @param test_case the test case to run
 * @return PASS if there are no assertion failures, FAIL if there is an assertion failure while running the test, UNRESOLVED otherwise.
 * @see #run_test()
 * @see #siglongjmp_fallback()
 */

static test_outcome run_test_with_siglongjmp(const test_suite suite, const test* test_case)
{
	int sigsetret;
	if (0 == test_case->test_case) {
		fprintf(stderr, "%s is a null test\n", current_test);
		return INVALID;
	}
	sigsetret = sigsetjmp(recovery_buffer, 1);
	VERBOSE_LOG("Test %s:%s checkpoint status %d\n", sigsetret);
	switch (sigsetret) { /* clean path */
		case 0:
			current_stage = SETUP;
			VERBOSE_LOG("Running %s:%s suite setup\n");
			suite.setup();
			VERBOSE_LOG("Running %s:%s setup\n");
			test_case->setup();
			current_stage = TEST;
			VERBOSE_LOG("Running %s:%s\n");
			test_case->test_case();
			current_stage = TEARDOWN;
			VERBOSE_LOG("Running %s:%s tear down\n");
			test_case->teardown();
			VERBOSE_LOG("Running %s:%s tear down\n");
			suite.teardown();
			return PASS;
			break;
		case SETUP:
			LOG_TEST_ERROR("Error in [%s:%s] during setup\n");
			sigsetret = sigsetjmp(recovery_buffer, 1);
			if (0 == sigsetret) {
				current_stage = TEARDOWN;
				VERBOSE_LOG("Running %s:%s setup failure tear down\n");
				test_case->teardown();
			}
			break;
		case TEST:
			LOG_TEST_ERROR("Test failure in [%s:%s] ***%s\n", TEST_OUTCOMES[FAIL]);
			sigsetret = sigsetjmp(recovery_buffer, 1);
			if (0 == sigsetret) {
				current_stage = TEARDOWN;
				VERBOSE_LOG("Running %s:%s test failure tear down\n");
				test_case->teardown();
			}
			return FAIL;
			break;
		case TEARDOWN:
			LOG_TEST_ERROR("Error in [%s:%s] during teardown\n");
			break;
		default:
			LOG_TEST_ERROR("Error in [%s:%s] with unknown failure type %u\n", sigsetret);
			break;
	}

	return UNRESOLVED;
}

/**
 * Implement the appropriate statistics/logging for a given test
 * outcome.
 *
 * @param outcome the tests outcome
 */

void handle_outcome(test_outcome outcome)
{
	switch (outcome) {
		case PASS:
			++(test_results->passed);
			break;
		case FAIL:
			++(test_results->failed);
			break;
		case UNRESOLVED:
			++(test_results->unresolved);
			break;
		case INVALID:
		default:
			fprintf(stderr, "INVALID outcome\n");
			abort();
			break;
	}
	fprintf(stderr, "%s\n", TEST_OUTCOMES[outcome]);
}

/**
 * Run some tests from a test suite.
 *
 * @param suite the suite which contains the tests
 * @param test_name the name of the test to run, or NULL to run all tests in the suite
 * @return FALSE if there was a problem finding the tests, otherwise TRUE
 */

static BOOLEAN run_tests(test_suite suite, const char* test_name)
{
	current_suite = suite.name;
	if (NULL == test_name) {
		int i;
		
		for (i = 0; i < suite.num_tests; ++i) {
			++(test_results->run);
			current_test = suite.tests[i].name;
			printf("Running test: %s\n", current_test);
			handle_outcome(test_runner(suite, &(suite.tests[i])));
			current_test = NULL;
		}
	} else {
		int i;
		for (i = 0; i < suite.num_tests; ++i) {
			if (strcmp(test_name, suite.tests[i].name) == 0) {
				current_test = suite.tests[i].name;
				printf("Running test: %s\n", current_test);
				handle_outcome(test_runner(suite, &(suite.tests[i])));
				current_test = NULL;
				return TRUE;
			}
		}

		fprintf(stderr, "Unable to find test: %s\n", test_name);
		return FALSE;
	}
	current_suite = NULL;

	return TRUE;
}

int main(int argc, char** argv)
{
	const char* test_name = NULL;
	int i = 1;
	test_suite suite;

	test_runner = run_test_in_child;
	fallback_function = exit_fallback;

	while (i < argc) {
		if (*(argv[i]) != '-') {
			test_name = argv[i];
			break;
		} else {
			switch (*(argv[i]+1)) {
				case 'f':
					test_runner = run_test_in_child;
					break;
				case 's':
					test_runner = run_test;
					break;
				case 'S':
					test_runner = run_test_with_siglongjmp;
					fallback_function = siglongjmp_fallback;
					break;
				case 'v':
					verbose_mode = TRUE;
					break;
				case 'h':
					printUsage(argv[0]);
					return OK;
				default:
					fprintf(stderr, "Invalid option: %s\n", argv[i]);
					printUsage(argv[0]);
					return USAGE;
			}
		}
		++i;
	}

	if (!init_testing()) {
		fprintf(stderr, "Unable to initialize testing runtime\n");
		return TEST_INIT;
	}

	suite = get_suite();

	if (!run_tests(suite, test_name)) {
		return TEST_NOT_FOUND;
	}

	printResults();
	release_suite(suite);

	return (cleanup_testing()) ? OK : TEST_CLEANUP;
}

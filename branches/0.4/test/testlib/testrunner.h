#ifndef	TESTRUNNER_H_
#define	TESTRUNNER_H_

void testrunner_assert(BOOLEAN test,
					   const char* description,
					   const char* file,
					   unsigned int line);
void testrunner_fail(const char* description,
					 const char* file,
					 unsigned int line);

#endif 	    /* !TEST-PRIVATE_H_ */

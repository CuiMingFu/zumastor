#include <sys/types.h>

extern int diskread(int fd, void *data, size_t count, off_t offset);
extern int diskwrite(int fd, void const *data, size_t count, off_t offset);

extern int fdread(int fd, void *data, size_t count);
extern int fdwrite(int fd, void const *data, size_t count);


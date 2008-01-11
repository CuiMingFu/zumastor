
/* return codes */
#define SUCCESS_DELTA      0
#define UNKNOWN_ERROR     -1
#define BUFFER_SIZE_ERROR -2

int create_delta_chunk(void *buff1, void *buff2, void *delta, int buff_size, int *delta_size);
int apply_delta_chunk(void *buff1, void *buff2, void *delta, int buff_size, int delta_size);

#include <stdio.h>
#include <unistd.h>
#include "delta.h"
#include "xdelta/xdelta3.h"

int delta_chunk_helper(int (*func) (xd3_stream *), 
		       const uint8_t *input1, 
		       const uint8_t *input2, 
		       uint8_t *output, 
		       int max_size,
		       int input2_size,
		       int *output_size) {

	xd3_stream stream;
	xd3_config config;
	char const *err_msg;
	int ret = UNKNOWN_ERROR;

	*output_size = 0;

	xd3_init_config(&config, 0);
	config.winsize = max_size;

	err_msg = "config stream failed\n";
	if(xd3_config_stream(&stream, &config) != 0) 
		goto error;
	
	xd3_source source;
	source.name     = NULL;
	source.size     = max_size;
	source.ioh      = NULL;
	source.blksize  = max_size;
	source.curblkno = 0;
	source.curblk   = input1;
	source.onblk    = max_size;
	
	err_msg = "set_source failed\n";
	if(xd3_set_source(&stream, &source) != 0) 
		goto error;
	
	xd3_avail_input(&stream, input2, input2_size);

	while(ret != 0) {
		ret = func(&stream);
		switch (ret) {
		case XD3_INPUT:
			err_msg = "input needed? impossible\n";
			ret = UNKNOWN_ERROR;
			goto error;  
		case XD3_OUTPUT:
			/* write data */
			if(*output_size + stream.avail_out > max_size) {
				err_msg = "buffer too small to fit output data\n";
				xd3_consume_output(&stream);
				ret = BUFFER_SIZE_ERROR;
				goto error;
			}
			memcpy((void *)(output + *output_size), stream.next_out, stream.avail_out);
			*output_size = *output_size + stream.avail_out;
			xd3_consume_output(&stream);
    			continue;
  		case XD3_GETSRCBLK:
			ret = UNKNOWN_ERROR;
			break;
  		case XD3_GOTHEADER:
		case XD3_WINSTART:
			continue;
  		case XD3_WINFINISH:
    		/* no action necessary */
			ret = SUCCESS_DELTA;
			continue;
  		default:
			err_msg = "error in switch\n";
			ret = UNKNOWN_ERROR;
			goto error;
		}
	}

	xd3_close_stream(&stream);
	xd3_free_stream(&stream);		
	return ret;

error:
	xd3_close_stream(&stream);
	xd3_free_stream(&stream);
	return ret;
}

int create_delta_chunk(void *buff1, void *buff2, void *delta, int buff_size, int *delta_size) {
	return delta_chunk_helper(xd3_encode_input, buff1, buff2, delta, buff_size, buff_size, delta_size);
}

int apply_delta_chunk(void *buff1, void *buff2, void *delta, int buff_size, int delta_size) {
	int output_size, ret;

	ret = delta_chunk_helper(xd3_decode_input, buff1, delta, buff2, buff_size, delta_size, &output_size);
	
	return (ret == SUCCESS_DELTA) ? output_size : ret;
}

#ifdef _UNIT_TEST
int test_func(void) {

	char buff1[512], buff2[512], delta[512], newbuff2[512];
	int i, ret, size = 0;
		
	for(i = 0; i < 512; i++) {
		buff1[i] = 'A';
		buff2[i] = 'A';
		if(i % 20)
			buff2[i] = 'B';
	}

	i=0;
	while(i < 100) {
	ret = create_delta_chunk(buff1, buff2, delta, 512, &size);
	printf("size of delta is %d\n", size);
	
	if(ret < 0) {
		printf("someone screwed up.\n");
		return 1;
	}

	printf("two different, right? ");
	if(memcmp(buff2, newbuff2, 512) == 0)
		printf("No, this should not happen. Most depressing.\n");
	else
		printf("Yes, they are not the same. Excellent.\n");
	
	ret = apply_delta_chunk(buff1, newbuff2, delta, 512, size);
	
	printf("Generated new chunk from delta. Did it generate the correct chunk? ");
	if(memcmp(buff2, newbuff2, 512) == 0) 
		printf("Yes, however, so far, so good, so what.\n");
	else
		printf("Nope!\n");

	memset(newbuff2, 512, sizeof(char));
	i++;
	}
	return 0;
}

int main(void) {
	return test_func();	
}
#endif

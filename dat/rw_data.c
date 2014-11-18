#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "../communication.h"
#include "sockets.h"
#include "data_pack.h"

uint32_t get_file_size(const char *path)
{
	uint32_t file_size = 0;	
	struct stat statbuff;
	if(stat(path, &statbuff) < 0){
		return file_size;
	}else{
		file_size = statbuff.st_size;
	}
	return file_size;
}


int8_t load_data(char *path, uint32_t length, char *buf)
{
	uint32_t file_size = 0;
	FILE *fp = NULL;

	fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "open file(%s) failed!\n", path);
		return 3;
	}

	file_size = get_file_size(path);
	if (file_size != length) {
		fprintf(stderr, "load file(%s) error!\n", path);
		return 3;
	}
	
	if (fread(buf, sizeof(char), length, fp) < 0) {
		fprintf(stderr, "fread file(%s) error!\n", path);
		return 3;
	}

	fclose(fp);
	return 0;
}

int8_t save_data(int sock, char *path, uint64_t size)
{
	FILE *fp = NULL;
	uint32_t read_size = 0;
	uint64_t len = 0;
	char data[PACK_SIZE] = {0};

	fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "open file(%s) failed!\n", path);
    	return -1;
    }

	len = 0;
    while (len < size) {	
    	read_size = read(sock, data, PACK_SIZE);
        if(read_size <= 0) {
        	fclose(fp);
			return 3;
		}
		if (DEBUG) {	
        	fprintf(stderr, "total size(%lld), read size is:%lld\n",
						(long long int)size, (long long int)len);
		}
        	fwrite(data, sizeof(char), read_size, fp);
        	len += read_size;
    }

    fprintf(stderr, "save over!\n");
    fclose(fp);

	return 0;
}

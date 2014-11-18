#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "../communication.h"
#include "sockets.h"
#include "data_pack.h"
#include "con_ser.h"

#define RETRIES 3
#define CSMSECTIMEOUT 30000

static int rw_sock = -1;

static int8_t read_data_refresh_connection(uint32_t inode, uint32_t indx, uint64_t *chunkid, uint64_t *size)
{
	uint32_t ip;
	uint16_t port;
	int8_t status;

	if (rw_sock >= 0) {
		tcpclose(rw_sock);
		rw_sock = -1;
	}

	status = fs_readchunk(inode, indx, chunkid, size, &ip, &port);

	if (status != 0) {
		return 2;
	}

	if (*chunkid == 0 || *size <= 0) {
		fprintf(stderr, "chunkid or size error\n");
		return 3;
	}

	rw_sock = tcpsocket();
	if (rw_sock < 0) {
		fprintf(stderr,"can't create tcp socket\n");
		return 1;
	}
	if (tcpnodelay(rw_sock)<0) {
		fprintf(stderr,"can't set TCP_NODELAY\n");
	}
	if (tcpnumconnect(rw_sock, ip, port)<0) {
		fprintf(stderr,"can't connect to (%d.%d.%d.%d:%u)\n",
		ip/(256*256*256)%256, ip/(256*256)%256, ip/256%256, ip%256, port);
		tcpclose(rw_sock);
		rw_sock = -1;
		return 1;
	}
	fprintf(stderr,"connect to (%d.%d.%d.%d:%u)\n",
		ip/(256*256*256)%256, ip/(256*256)%256, ip/256%256, ip%256, port);
	return 0;
}

int8_t direct_readblock(const char *src_name, const char *dest_path, uint64_t chunkid, uint64_t size)
{
	FILE *fp = NULL;
	char buff[PACK_SIZE] = {0};
	uint8_t *ptr = NULL;
	uint8_t hdrbuff[13];
	uint32_t cmd;
	uint32_t length;
	uint32_t version = 0;
	uint8_t status = 0;
	uint64_t read_size = 0;
	char path[MAX_NAME_LEN] = {0};
	
	uint8_t *ibuff = NULL;

	ibuff = malloc(24);
	ptr = ibuff;

	PUT32BIT(CLITODAT_READ_INFO, ptr);
	PUT32BIT(16, ptr);
	PUT64BIT(chunkid, ptr);
	PUT64BIT(size, ptr);

if (DEBUG) {
uint32_t j = 0;
for(j = 0; j < 24; j++) {
	fprintf(stderr, "%u,", ibuff[j]);
}
fprintf(stderr, "rr\n"); 
} 

	if (tcptowrite(rw_sock, ibuff, 24, CSMSECTIMEOUT) != 24) {
		free(ibuff);
		fprintf(stderr, "readblock; tcpwrite error!\n");
		return 1;
	}
	free(ibuff);

	// 获取校验位
	if (read(rw_sock, hdrbuff, 13) != 13) {
		fprintf(stderr, "readblock; tcpread error\n");
		return 1;
	}
	ptr = hdrbuff;
	GET32BIT(cmd, ptr);
	GET32BIT(length, ptr);
	GET32BIT(version, ptr);
	GET8BIT(status, ptr);

	if (cmd != DATTOCLI_READ_INFO) {
		fprintf(stderr, "order data inconsistent\n");
		return 1;
	}

	if (status != 0) {
		fprintf(stderr, "status from data is error\n");
		return 1;
	}

	sprintf(path, "%s/%s", dest_path, strrchr(src_name, '/') + 1);

	fp = fopen(path, "wb+");
	if (fp == NULL) {
		fprintf(stderr, "open file(%s) failed!\n", path);
		return 1;
	}

	while (read_size < size) {
		int len = 0;
		len = read(rw_sock, buff, PACK_SIZE);

		fwrite(buff, sizeof(char), len, fp);
		read_size += len;
	}
	
	fprintf(stderr, "path: %s\n", path);
	fprintf(stderr, "total size(%llu), write size: %llu\n",
				(long long unsigned int)size, (long long unsigned int)read_size);
	fprintf(stderr, "Read over!\n");

	fclose(fp);
	return 0;
}

static uint32_t get_inode_by_name(const char *src)
{
	uint8_t status;
	uint8_t *path_name = NULL;
	uint16_t path_len;
	uint32_t inode = 0;

	path_len = strlen(src);
	path_name = malloc(path_len*sizeof(uint8_t));
	memcpy(path_name, src, path_len);

	status = fs_judge_name(path_len, path_name, &inode);
	free(path_name);

	if (status == 0) {
		return inode;
	}

	return 0;
}

int read_data(const char *src, const char *dest) 
{
	uint8_t cnt = 0;
	int8_t err = 0;
	uint32_t inode = 0;	
	uint32_t indx = 0;
	uint64_t file_size = 0;
	uint64_t chunkid = 0;
	int8_t status = 0;


	inode = get_inode_by_name(src); // 显示用inode代替

	if (inode <= 0) {
		fprintf(stderr, "inode(%d) is error\n", inode);
		return 1;
	}

	while (cnt<RETRIES) {
		cnt++;
		err = read_data_refresh_connection(inode, indx, &chunkid, &file_size);
		if (err == 0) {
			break;
		}else if (err == -1) {
			fprintf(stderr, "connect is error\n");
		}else if (err == -2) {
			fprintf(stderr, "status from fs server is error\n");
		}
		fprintf(stderr, "file:%u,- can't connect to data (try counter: %u)\n",inode, cnt);
			sleep(2);
		}
		if (cnt>=RETRIES) {
			return err;
		}

		// 一次性读写	
	status = direct_readblock(src, dest, chunkid, file_size);

	return status;
}



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
#include "con_ser.h"

#define RETRIES 3
#define CSMSECTIMEOUT 30000

static int rw_sock = -1;

static int write_data_refresh_connection(uint32_t inode, uint32_t indx, uint64_t *chunkid)
{
	uint32_t ip;
	uint16_t port;
	uint8_t status;

	if (rw_sock >= 0) {
		tcpclose(rw_sock);
		rw_sock = -1;
	}

	status = fs_writechunk(&ip,&port, chunkid, inode, indx);
	if (status != 0) {
		fprintf(stderr, "status error\n");
		return -2;
	}

	rw_sock = tcpsocket();
	if (rw_sock < 0) {
		fprintf(stderr,"can't create tcp socket\n");
		return -1;
	}
	if (tcpnodelay(rw_sock)<0) {
		fprintf(stderr,"can't set TCP_NODELAY\n");
	}
	if (tcpnumconnect(rw_sock, ip, port)<0) {
		fprintf(stderr,"can't connect to (%d.%d.%d.%d:%u)\n",
		ip/(256*256*256)%256, ip/(256*256)%256, ip/256%256, ip%256, port);
		tcpclose(rw_sock);
		rw_sock = -1;
		return -1;
	}
	return 0;
}

uint64_t get_file_size(const char *path)
{
        uint64_t file_size = 0;
        struct stat statbuff;
        if(stat(path, &statbuff) < 0){
                return file_size;
        }else{
                file_size = statbuff.st_size;
        }
        return file_size;
}

uint32_t direct_writeblock(const char *src_path, uint64_t chunkid, uint64_t *t_size)
{
	uint8_t *ptr = NULL;
	uint8_t *ibuff = NULL;
	int8_t status = 0;
	uint64_t size;
	uint64_t len;
	uint32_t read_size = 0;
	FILE *fp = NULL;
	char *p_data = NULL;

    fp = fopen(src_path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "open file(%s) failed!\n", src_path);
        return 3;
    }

    size = get_file_size(src_path);
    if (size <= 0) { // 大小为0的文件丢弃
        fprintf(stderr, "get file(%s) error!\n", src_path);
		fclose(fp);
        return 3;
    }
 
	ibuff = malloc(24);
	ptr = ibuff;

	PUT32BIT(DRWTODAT_WRITE_INFO, ptr);
	PUT32BIT(16, ptr);
	PUT64BIT(chunkid, ptr);
	PUT64BIT(size,ptr);

	if (tcptowrite(rw_sock, ibuff, 24, CSMSECTIMEOUT) != 24) {
		free(ibuff);
		fclose(fp);
		fprintf(stderr, "write data to dat error!\n");
		return 3;
	}
	free(ibuff);
	// 等待dat端的回复
	uint8_t hdr[13];
	uint8_t *ptr1 = NULL;
	uint32_t cmd1 = 0;
	uint32_t len1 = 0;
	uint32_t version1 = 0;

	ptr1 = hdr;
	if (read(rw_sock, hdr, 13) != 13) {
		fprintf(stderr, "read data from dat error\n");
	}
	GET32BIT(cmd1, ptr1);
	GET32BIT(len1, ptr1);
	GET32BIT(version1, ptr1);
	GET8BIT(status, ptr1);
int i = 0;
for(i=0; i< 13; i++) {
	fprintf(stderr, "%u-", hdr[i]);
}
fprintf(stderr, "hdr end\n");


	if (cmd1 != DATTODRW_WRITE_INFO || status != 0) {
		fprintf(stderr, "dat not ready!\n");
		return 1;
	} 

	ibuff = malloc(W_PACK_SIZE*sizeof(uint8_t));
	ptr = ibuff;
	p_data = malloc(W_PACK_SIZE*sizeof(char));
	len = 0;
  	while (len < size) {
        read_size = fread(p_data, sizeof(char), W_PACK_SIZE, fp);
	 	if (read_size < 0) {
            fprintf(stderr, "fread file(%s) error!\n", src_path);
		 	fclose(fp);
			free(ibuff);
			free(p_data);
            return 3;
        }
		memcpy(ptr, p_data, read_size);
		if (tcptowrite(rw_sock, ibuff, read_size, CSMSECTIMEOUT) != read_size) {
			fprintf(stderr, "readblock; tcpwrite error!\n");
			free(ibuff);
			free(p_data);
		 	fclose(fp);
			return 3;
		}
		if (DEBUG) {
			fprintf(stderr, "total size(%llu), read size: %llu\n",
					(unsigned long long int)size, (unsigned long long int)len);
		}
		len += read_size;
	}
	fprintf(stderr, "Write over!\n");
	
	*t_size = size;
    fclose(fp);
	free(ibuff);
	free(p_data);

	return status;
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
        if (status == 0) {
			fprintf(stderr, "file exist, inode is %d\n", inode);
        	free(path_name);
            return inode;
        }

		// create new file	
		status = fs_creat_file(path_len, path_name, &inode);
        if (status == 0) {
			fprintf(stderr, "create new file, inode is %d\n", inode);
        	free(path_name);
            return inode;
        }
	
		free(path_name);
        return 0;
}

int write_data(const char *src, const char *dest) 
{
	uint8_t cnt = 0;
	int8_t err = 0;
	uint32_t inode = 0;	
	uint64_t chunkid = 0;
	uint32_t indx = 0;
	uint64_t size = 0;
	int8_t status = 0;
	char combo_path[MAX_NAME_LEN] = {0};

	sprintf(combo_path, "%s/%s", dest, strrchr(src, '/')+1);

	inode = get_inode_by_name(combo_path); // 显示用inode代替

	if (inode == 0) {
		fprintf(stderr, "dest(%s) creat failed\n", dest);
	}

// 小于64M
	while (cnt<RETRIES) {
		cnt++;
		err = write_data_refresh_connection(inode, indx, &chunkid);
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
	
	// 直写	
	status = direct_writeblock(src, chunkid, &size);
	if (status == 0) {
		// 修改ser中文件的大小
		fs_writechunk_end(inode, indx, size);
	}
	// 关闭套接字
	if (rw_sock >= 0) {
		tcpclose(rw_sock);
		rw_sock = -1;
	}

	return 0;
}

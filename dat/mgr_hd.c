#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dat.h"

#define HASHSIZE 32768
#define HASHPOS(chunkid) ((chunkid)&0x7FFF)

#define FS_DIR "/home/fs"
#define FS_LEN 8
#define MAX_PATH_LEN 1024

typedef struct chunk {
	char *filename;
	uint64_t chunkid;

	struct chunk *next;
}chunk;

static chunk *hashtab[HASHSIZE];

int8_t create_file(uint64_t chunkid)
{	
	chunk *new = NULL;
	uint32_t hashpos = HASHPOS(chunkid);
	char sub_dir;
	int fd = -1;

	sub_dir = chunkid&0xF;
	if (sub_dir < 10) {
		sub_dir += '0';
	} else {
		sub_dir -= 10;
		sub_dir += 'a';
	}

	new = (chunk*)malloc(sizeof(chunk));
	new->filename = malloc(FS_LEN+30);
	memcpy(new->filename, FS_DIR, FS_LEN);

	sprintf(new->filename+FS_LEN, "/%c/chunk_%016llX.cfs", sub_dir,
				   (long long unsigned int)chunkid);
	new->filename[29+FS_LEN] = 0;
	new->chunkid = chunkid;

	fd = open(new->filename,O_CREAT | O_TRUNC | O_WRONLY,0666);
	if (fd<0) { // 创建失败则释放该节点
		free(new->filename);
		free(new);
		return 1;
	}
if (1) {
	fprintf(stderr, "create file(%s) success\n", new->filename);
}
	close(fd);
	new->next = hashtab[hashpos];
	hashtab[hashpos] = new;
	
	return 0;	
}

void hd_init()
{
	int32_t i = 0;
	char path[MAX_PATH_LEN] = {0};


	for (i = 0; i < HASHSIZE; i++) {
		hashtab[i] = NULL;
	}

	/* *clear all data* */
	system("rm -rf /home/fs/*");
	// 创建文件夹，由于存储chunk块
	//
	for (i = 0; i < 16; i++) {
		if (i < 10) {
			sprintf(path, "%s/%c", FS_DIR, '0'+i);
		} else {
			sprintf(path, "%s/%c", FS_DIR, 'a'+i-10);
		}
		mkdir(path, 0755);
	}	

}


int8_t get_path_by_chunkid(char *path, uint64_t chunkid)
{
	uint32_t hashpos = HASHPOS(chunkid);
	chunk *chunkit = NULL;

	for (chunkit = hashtab[hashpos]; chunkit; chunkit = chunkit->next) {
		if (chunkit->chunkid == chunkid) {
			strcpy(path, chunkit->filename);
			return 0;
		}
	}
	
	return 1;
}

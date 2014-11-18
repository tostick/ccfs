#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "con_dat.h"

#define HASHSIZE 65536
#define HASHPOS(chunkid) (((uint32_t)chunkid)&0xFFFF)


typedef struct chunk {
	uint64_t chunkid;

	struct chunk *next;
} chunk;

extern dat_info *dat_info_head;

static chunk *chunkhash[HASHSIZE];
uint64_t next_chunkid = 11;

void chunk_init()
{
	uint32_t i;

	for (i=0; i<HASHSIZE; i++) {
		chunkhash[i] = NULL;
	}
}

/* 创建新的chunk，选择合适的dat
 * 目前只有一个sock，不必作出选择,将data的sock共享即可
 * */
int8_t create_new_chunk(uint64_t *chunkid)
{
	dat_info *info;
	uint16_t link_nums = 0;

	if (dat_info_head == NULL) {
		fprintf(stderr, "no data node\n");
		return 1;
	}
	
	for (info = dat_info_head; info; info = info->next) {
		if (info->mode != KILL) {
			link_nums ++;
		}
		break;
	}

	if (link_nums == 0) {
		fprintf(stderr, "data node in use is zero\n");
		return 2;
	}

	next_chunkid ++;
	*chunkid = next_chunkid;

	send_chunkid(*chunkid, info);

	return 0;
}

#ifndef _SER_H_
#define _SER_H_

#include <stdint.h>

void ser_init(char *ip, char *host);

void close_service();

uint8_t fs_getattr(uint32_t inode,uint8_t attr[35]);

uint8_t fs_getdir(uint32_t inode,uint32_t uid,uint32_t gid,uint8_t **dbuff,uint32_t *dbuffsize);

uint8_t fs_lookup(uint32_t parent,uint8_t nleng,const uint8_t *name,uint32_t uid,uint32_t gid,
		uint32_t *inode,uint8_t attr[35]);

void fs_statfs(uint64_t *totalspace,uint64_t *availspace,uint64_t *trashspace,
		uint64_t *reservedspace,uint32_t *inodes);

uint8_t fs_readchunk(uint32_t inode,uint32_t indx,uint64_t *chunkid,
				uint64_t *size, uint32_t *ip,uint16_t *port);

uint8_t fs_writechunk(uint32_t *ip,uint16_t *port, uint64_t *chunkid,
				uint32_t inode, uint32_t indx);

uint8_t fs_writechunk_end(uint32_t inode, uint32_t indx, uint32_t size);

uint8_t fs_info(uint32_t inode,uint32_t uid,uint32_t gid,uint8_t **dbuff,uint32_t *dbuffsize); 

uint8_t fs_judge_name(uint16_t path_len, uint8_t *name,uint32_t *inode);

uint8_t fs_creat_file(uint16_t path_len, uint8_t *name,uint32_t *inode);

#endif

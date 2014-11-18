#ifndef _SER_H_
#define _SER_H_

void ser_init(char *ip, char *host);

uint8_t fs_getattr(uint32_t inode,uint8_t attr[35]);

uint8_t fs_getdir(uint32_t inode,uint32_t uid,uint32_t gid,
				uint8_t **dbuff,uint32_t *dbuffsize);

uint8_t fs_lookup(uint32_t parent,uint8_t nleng,const uint8_t *name,
				uint32_t uid,uint32_t gid,uint32_t *inode,uint8_t attr[35]);

void fs_statfs(uint64_t *totalspace,uint64_t *availspace,uint64_t *trashspace,
				uint64_t *reservedspace,uint32_t *inodes);

uint8_t fs_readchunk(uint32_t inode,uint32_t indx,uint64_t *length,uint64_t *chunkid,
				uint32_t *version,uint32_t *csip,uint16_t *csport,
				uint16_t *nleng, uint8_t **name, uint8_t *file_type, uint32_t *file_size); 

#endif

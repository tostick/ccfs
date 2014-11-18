#ifndef  _FS_H_
#define  _FS_H_

int8_t load_fs();

int8_t fs_getattr(uint32_t inode,uint8_t attr[35]);

int8_t fs_lookup(uint32_t inode, uint8_t *name, uint16_t nleng, 
				uint8_t attr[35], uint32_t *child_inode);

int8_t getdir_size(uint32_t inode, uint32_t *len);

int8_t fs_getdir(uint32_t inode, uint8_t *name_info);

int8_t getdir_size_from_drw(uint32_t inode, uint32_t *len);

int8_t fs_getdir_from_drw(uint32_t inode, uint8_t *name_info);

void fs_statfs(uint64_t *totalspace,uint64_t *availspace,
				uint64_t *trspace,uint64_t *respace,uint32_t *inodes);

int8_t fs_read_chunk(uint32_t inode, uint32_t index, uint64_t *chunkid, uint64_t *file_size);

int8_t fs_look_name(uint8_t *name, uint16_t nleng, uint32_t *new_inode);

int8_t add_chunkid_to_fs(uint32_t inode, uint32_t index, uint64_t *chunkid);

int8_t fs_creat_file(uint8_t *name, uint16_t len, uint32_t *new_inode);

int8_t save_fs_config_to_local();

int8_t change_file_size(uint32_t inode, uint32_t index, uint64_t size);

#endif

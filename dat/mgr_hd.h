#ifndef _MGR_HD_H_
#define _MGR_HD_H_

void hd_init();
int8_t create_file(uint64_t chunkid);
int8_t get_path_by_chunkid(char *path, uint64_t chunkid);


#endif

#ifndef _RW_DATA_H_
#define _RW_DATA_H_

int8_t load_data(char *path, uint32_t length, char *buf);
int8_t save_data(int sock, char *path, uint64_t size);

#endif

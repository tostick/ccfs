#ifndef _CON_DAT_H_
#define _CON_DAT_H_

#include <stdint.h>

enum {KILL, HEADER, DATA};

typedef struct packets {
	uint8_t *start_ptr;
	uint32_t bytes;
	uint8_t *packet;
	struct packets *next;
} packets;

typedef struct dat_info {
	int sock;
	int mode;
	uint32_t r_lasttime;
	uint32_t w_lasttime;
	
	uint8_t buff[8];

	packets in_data_packet;
	packets *out_data_packet;
	struct dat_info *next;
}dat_info;


int connect_dat();
int8_t send_chunkid(uint64_t chunkid, dat_info *info);


#endif

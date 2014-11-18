#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include "ser.h"
#include "fs.h"
#include "sockets.h"
#include "data_pack.h"
#include "con_dat.h"
#include "chunks.h"
#include "../communication.h"

#define MaxPacketSize 1000000
#define MAX_NAME_LEN 1024

dat_info *dat_info_head = NULL;

static pthread_mutex_t sock_lock;
static pthread_t rthid;

static int exiting = 0;
static int lsock = 0;
static int dat_errors = 0;
static int disconnect = 1;

void write_to_dat(dat_info *info);

int dat_desc(fd_set *rset, fd_set *wset)
{
	int max = 0;
	int i = 0;
	dat_info *info;

	if (exiting == 0) {
		FD_SET(lsock, rset);
		max = lsock;
	} else {
		max = -1;
	}

	for (info = dat_info_head; info; info = info->next) {
		i = info->sock;
		if (exiting == 0) {
			FD_SET(i, rset);
			if (i > max) {
				max = i;
			}
		}

		if (info->out_data_packet != NULL) {
			FD_SET(i, wset);
			if (i < max) {
				max = i;
			}
		}
	
	}

	return max;
}


uint8_t* create_dat_packet(dat_info *info,uint32_t type,uint32_t size) 
{
	packets *out_packet = NULL;
	uint8_t *ptr;
	uint32_t psize;

	out_packet=(packets*)malloc(sizeof(packets));
	if (out_packet==NULL) {
		return NULL;
	}
	psize = size+8;
	out_packet->packet=malloc(psize);
	out_packet->bytes = psize;
	if (out_packet->packet==NULL) {
		free(out_packet);
		return NULL;
	}
	ptr = out_packet->packet;
	PUT32BIT(type,ptr);
	PUT32BIT(size,ptr);
	out_packet->start_ptr = (uint8_t*)(out_packet->packet);
	out_packet->next = NULL;

	info->out_data_packet = out_packet;
	return ptr;
}

void get_dat_size(dat_info *info, uint8_t *data, uint32_t length)
{
	uint64_t totalspace,availspace;
	uint32_t msgid;
	uint32_t version = 0;
	uint32_t status = 0;
	uint8_t *ptr = NULL;

	if (length != 20 ) {
		fprintf(stderr,"DATTOSER_DISK_INFO - wrong size (%d/4)\n",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET64BIT(totalspace,data);
	GET64BIT(availspace,data);

	fprintf(stderr, "total space:%llu\n", (long long unsigned int)totalspace);
	fprintf(stderr, "avail space:%llu\n", (long long unsigned int)availspace);

	// 答复，以确保连接是否仍建立 
	ptr = create_dat_packet(info, SERTODAT_DISK_INFO, 8);
	PUT32BIT(version, ptr);
	PUT32BIT(status, ptr);

	write_to_dat(info);
}

// first consider one socket
int8_t send_chunkid(uint64_t chunkid, dat_info *info) 
{
	uint32_t version = 0;
	uint8_t *ptr = NULL;

	pthread_mutex_lock(&sock_lock);
	ptr = create_dat_packet(info, SERTODAT_CREAT_CHUNK, 12);
	PUT32BIT(version, ptr);
	PUT64BIT(chunkid, ptr);

	write_to_dat(info);
	return 0;
}

void get_dat_chunk_status(dat_info *info, uint8_t *data, uint32_t length)
{
	uint32_t status;
	uint32_t msgid;


	if (length != 8 ) {
		fprintf(stderr,"DATTOSER_CREAT_CHUNK - wrong size (%d/4)\n",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET32BIT(status,data);

	if (status != 0) {
		fprintf(stderr, "dat create chunk failed\n");
		dat_errors ++;
	}
	pthread_mutex_unlock(&sock_lock);
}

void analyze_dat_packet(dat_info *info,uint32_t type,uint8_t *data,uint32_t length) 
{
	switch (type) {
		case ANTOAN_NOP:
			break;
		case DATTOSER_DISK_INFO:
			get_dat_size(info, data, length);
			break;
		case DATTOSER_CREAT_CHUNK:
			get_dat_chunk_status(info, data, length);
			break;
		default:
			dat_errors ++;
			fprintf(stderr, "not recognition type(%d)\n", type);
	}
}

void read_from_dat(dat_info *info)
{
	int i = 0;
	uint32_t size = 0;
	uint32_t type = 0;
	uint8_t *ptr = NULL;

	i =  read(info->sock, info->in_data_packet.start_ptr, 
					info->in_data_packet.bytes);
		
	
int j = 0;
if (type) {
	fprintf(stderr, "dat read, size %d mode: %d ", info->in_data_packet.bytes, info->mode);
	for (j = 0; j < i; j++) {
		fprintf(stderr, "%u-", info->in_data_packet.start_ptr[j]);
	}
	fprintf(stderr, "\n");
}

	if (i == 0) {
		info->mode = KILL;
		return;
	}

	if (i < 0) {
		info->mode = KILL;
		fprintf(stderr, "read error from dat client\n");
		return;
	}

	info->in_data_packet.start_ptr += i; // 保留第一次接受的8字节头
	info->in_data_packet.bytes -= i;

	if (info->in_data_packet.bytes > 0) {
		return;
	}
	
	if (info->mode==HEADER) {
		ptr = info->buff+4;
		GET32BIT(size,ptr);

		if (size>0) {
			if (size > MaxPacketSize) {
				fprintf(stderr,"dat: packet too long (%u/%u)",size,MaxPacketSize);
				info->mode = KILL;
				return;
			}
			info->in_data_packet.packet = malloc(size);
			if (info->in_data_packet.packet==NULL) {
				fprintf(stderr ,"dat: out of memory");
				info->mode = KILL;
				return;
			}
			info->in_data_packet.bytes = size;
			info->in_data_packet.start_ptr = info->in_data_packet.packet;
			info->mode = DATA;
			return;
		}
		info->mode = DATA;
	}

	if (info->mode==DATA) {
		ptr = info->buff;
		GET32BIT(type,ptr);
		GET32BIT(size,ptr);

	fprintf(stderr, "type=%d\n", type);
	fprintf(stderr, "size=%d\n", size);
		info->mode = HEADER;
		info->in_data_packet.bytes = 8;
		info->in_data_packet.start_ptr = info->buff;

		analyze_dat_packet(info, type, info->in_data_packet.packet, size);

		if (info->in_data_packet.packet) {
			free(info->in_data_packet.packet);
		}
		info->in_data_packet.packet=NULL;
	}
}

void write_to_dat(dat_info *info)
{
	int i = 0;
	packets *pack = NULL;

	pack = info->out_data_packet;
	if (pack == NULL) {
		return;
	}

	i =  write(info->sock, pack->start_ptr, pack->bytes);

fprintf(stderr, "write: %d bytes: ", pack->bytes);
int j = 0;
for (j = 0; j < i; j++) {
	fprintf(stderr, "%u-", pack->start_ptr[j]);
}
fprintf(stderr, "\n");

	if (i < 0 || i != pack->bytes) {
		info->mode = KILL;
		fprintf(stderr, "write error to client\n");
		return;
	}
	pack->start_ptr += i;
	pack->bytes -= i;

	if (pack->bytes > 0) {
		fprintf(stderr, "write to dat, data send imcomplete\n");
		return;
	}
	free(pack->packet);
	free(pack);
	info->out_data_packet = NULL;  // NULL after data send
}

void dat_serv(fd_set *rset, fd_set *wset)
{
	dat_info *info = NULL;
	dat_info *pre_info = NULL;
	int ns = 0;
	uint32_t now;

	now = main_time();

	if (FD_ISSET(lsock, rset)) {
		ns = tcpaccept(lsock);
		if (ns < 0) {
			fprintf(stderr, "accept dat error\n");
			exit(1);
		} else {
			disconnect = 0;

			info = (dat_info *)malloc(sizeof(dat_info));	
			info->next = dat_info_head;
			dat_info_head = info;
			info->sock = ns;
			info->mode = HEADER;
			info->r_lasttime = now;
			info->w_lasttime = now;
			info->in_data_packet.next = NULL;
			info->in_data_packet.bytes = 8;
			info->in_data_packet.start_ptr = info->buff;
			info->in_data_packet.packet = NULL;
			info->out_data_packet = NULL;
		fprintf(stderr, "----------sock:%d---------\n", info->sock);
		}
	}

// 检查是否有dat端脱离
/*
	for (info=dat_info_head; info; info=info->next) {
		uint32_t version = 0;
		uint8_t *ptr = NULL;
	
		ptr = create_dat_packet(info, ANTOAN_NOP, 8);
		PUT32BIT(version, ptr);
		PUT32BIT(version, ptr);

		write_to_dat(info);	
		
		sleep(4);
	} */

	info = dat_info_head;
	while (info) {
		if (info->mode == KILL) {
		fprintf(stderr, "---------unsock:%d---------\n", info->sock);
			tcpclose(info->sock);
			if (info->in_data_packet.packet) {
				free(info->in_data_packet.packet);
			}

			if (info->out_data_packet) {
				if (info->out_data_packet->packet) {
					free(info->out_data_packet->packet);
				}
			}
			if (info == dat_info_head) {
				dat_info_head = dat_info_head->next;
				free(info);
				info = dat_info_head;
			} else if (info->next == NULL) {
				pre_info->next = NULL;
				free(info);
				break;
			} else {
				pre_info->next = info->next;
				free(info);
				info = pre_info->next;
			}
		} else {
			pre_info = info;
			info = info->next;
		}
	} 
}


void *receive_thread(void *arg)
{
	dat_info *info = NULL;
	while (1) {
		sleep(1);
		for (info = dat_info_head; info; info = info->next) {
			if (info->mode != KILL) {
				read_from_dat(info);
			}
		}
	}
}

int connect_dat()
{
	int ret = 0;
	char *host = "127.0.0.1";
	char *port = "2016";

	exiting = 0;
	dat_info_head = NULL;
	
	chunk_init();
	pthread_mutex_init(&sock_lock,NULL);

	lsock = tcpsocket();

	ret = tcplisten(lsock, host, port, 5);
	if (ret < 0) {
		fprintf(stderr, "listen dat error\n");
	}

	register_cli(dat_desc, dat_serv);	

	fprintf(stderr, "init ser complete\n");
	// 创建接收数据线程, 因为不似master一对一模式
	pthread_create(&rthid, NULL, receive_thread, NULL);

	return 0;
}

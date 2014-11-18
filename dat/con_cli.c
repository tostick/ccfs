#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "dat.h"
#include "rw_data.h"
#include "sockets.h"
#include "data_pack.h"
#include "mgr_hd.h"
#include "../communication.h"

#define MaxPacketSize 1000000
#define MAX_NAME_LEN 1024
#define MAX_PATH_LEN 1024

enum {KILL, HEADER, DATA};

static int exiting = 0;
static int lsock = 0;


typedef struct packets {
	uint8_t *start_ptr;
	uint32_t bytes;
	uint8_t *packet;
	struct packets *next;
} packets;

typedef struct cli_info {
	int sock;
	int mode;
	uint32_t r_lasttime;
	uint32_t w_lasttime;
	
	uint8_t buff[8];

	packets in_data_packet;
	packets *out_data_head;
	packets **out_data_tail;
	struct cli_info *next;
}cli_info;

static cli_info *cli_info_head = NULL;

void write_to_cli(cli_info *info);


int64_t dat_write(int sock,char *buff,uint64_t leng) {
	uint64_t sent=0;
	uint64_t i = 0;
	while (sent < leng) {
		i = write(sock, buff+sent, leng-sent);
		if (i <= 0) {
			return i;
		}
		sent += i;
	}
	return sent;
}

int cli_desc(fd_set *rset, fd_set *wset)
{
	int max = 0;
	int i = 0;
	cli_info *info;

	if (exiting == 0) {
		FD_SET(lsock, rset);
		max = lsock;
	} else {
		max = -1;
	}

	for (info = cli_info_head; info; info = info->next) {
		i = info->sock;
		if (exiting == 0) {
			FD_SET(i, rset);
			if (i > max) {
				max = i;
			}
		}

		if (info->out_data_head != NULL) {
			FD_SET(i, wset);
			if (i < max) {
				max = i;
			}
		}
	
	}

	return max;
}


uint8_t* create_cli_packet(cli_info *info,uint32_t type,uint32_t size) {
	packets *out_packet;
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
	*(info->out_data_tail) = out_packet;
	info->out_data_tail = &(out_packet->next);
	return ptr;
}

// send data size by cli decide
void cc_send_data(cli_info *info,uint8_t *data,uint32_t length) {
	uint8_t *ptr = NULL;
	char *buf = NULL;
	uint8_t status;
	uint64_t chunkid;
	uint64_t file_length;
	char path[MAX_PATH_LEN] = {0};

	if (length != 16) {
		fprintf(stderr, "CLITODAT_DATA_INFO - wrong size (%d)\n",length);
		info->mode = KILL;
		return;
	}
	
	GET64BIT(chunkid, data);
	GET64BIT(file_length, data);

	// 判断数据是否存在并获取路径名
	status = get_path_by_chunkid(path, chunkid);
//	status = judge_data_exist(chunkid, file_length);
	if (status == 0) {
		fprintf(stderr, "path:%s, length:%llu\n", path, (long long unsigned int)file_length);
	} else {
		fprintf(stderr, "chunkid unexist!\n");
	}

	ptr = create_cli_packet(info,DATTOCLI_READ_INFO,5);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet");
		info->mode = KILL;
		return;
	}
	PUT32BIT(0,ptr);
	PUT8BIT(status,ptr);
	write_to_cli(info);

	if (status == 0) {
		buf = malloc(file_length);
		status = load_data(path, file_length, buf);
		if (dat_write(info->sock, buf, file_length) != file_length) {
			fprintf(stderr, "write data error, length(%llu)\n", (long long unsigned int)file_length);
			info->mode = KILL;
		}
		free(buf);
	}
}

// recv data size from drw
void cc_recv_data(cli_info *info,uint8_t *data,uint32_t length) {
	int8_t status;
	char path[MAX_PATH_LEN] = {0};
	uint64_t chunkid;
	uint64_t file_length;
	uint8_t *ptr = NULL;

	if (length < 16) {
		fprintf(stderr, "DRWTODAT_WRITE_INFO - wrong size (%d)\n",length);
		info->mode = KILL;
		return;
	}
	
		
	GET64BIT(chunkid, data);
	GET64BIT(file_length, data);

	// 获取路径名
	status = get_path_by_chunkid(path, chunkid);
	if (status == 0) {
		fprintf(stderr, "path:%s, length:%llu\n", path, (long long unsigned int)file_length);
	} else {
		fprintf(stderr, "chunkid unexist!\n");
	}

	if (status != 0) {
		fprintf(stderr, "save data failed\n");
	}

	ptr = create_cli_packet(info,DATTODRW_WRITE_INFO,5);
	if (ptr	== NULL) {
		fprintf(stderr,"can't allocate memory for packet");
		info->mode = KILL;
		return;
	}

	PUT32BIT(0,ptr);
	PUT8BIT(status,ptr);
		
	write_to_cli(info);
	if (status == 0) {
		status = save_data(info->sock, path, file_length);
	}

}


void analyze_cli_packet(cli_info *info,uint32_t type,uint8_t *data,uint32_t length) 
{
	switch (type) {
		case ANTOAN_NOP:
			break;
		case CLITODAT_READ_INFO:
			cc_send_data(info, data, length);
			break;
		case DRWTODAT_WRITE_INFO:
			cc_recv_data(info, data, length);
			break;
		default:
			info->mode = KILL;
			fprintf(stderr, "not recognition type(%d)", type);

	
	}
}

void read_from_cli(cli_info *info)
{
	int i = 0;
	uint32_t size = 0;
	uint32_t type = 0;
	uint8_t *ptr = NULL;

	fprintf(stderr, "read size %d\n", info->in_data_packet.bytes);
	i =  read(info->sock, info->in_data_packet.start_ptr, info->in_data_packet.bytes);
		
	
int j = 0;
if (DEBUG) {
	fprintf(stderr, "read, size %d mode: %d ", info->in_data_packet.bytes, info->mode);
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
		fprintf(stderr, "read error from client\n");
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
				fprintf(stderr,"cli: packet too long (%u/%u)",size,MaxPacketSize);
				info->mode = KILL;
				return;
			}
			info->in_data_packet.packet = malloc(size);
			if (info->in_data_packet.packet==NULL) {
				fprintf(stderr ,"cli: out of memory");
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

		analyze_cli_packet(info, type, info->in_data_packet.packet, size);

		if (info->in_data_packet.packet) {
			free(info->in_data_packet.packet);
		}
		info->in_data_packet.packet=NULL;
		return;
	}

}


void write_to_cli(cli_info *info)
{
	int i = 0;
	packets *pack = NULL;

fprintf(stderr, "write to drw\n");
	pack = info->out_data_head;
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

	if (i < 0) {
		info->mode = KILL;
		fprintf(stderr, "write error to client\n");
		return;
	}
	pack->start_ptr += i;
	pack->bytes -= i;

	if (pack->bytes > 0) {
		return;
	}
	free(pack->packet);
	info->out_data_head = pack->next;
	if (info->out_data_head == NULL) {
		info->out_data_tail = &(info->out_data_head);
	}

	free(pack);
}

void cli_serv(fd_set *rset, fd_set *wset)
{
	cli_info *info, **p_info;
	packets *ptr,*patr;
	int ns = 0;
	uint32_t now;

	now = main_time();

	if (FD_ISSET(lsock, rset)) {
		ns = tcpaccept(lsock);
		if (ns < 0) {
			fprintf(stderr, "accept cli error\n");
			exit(1);
		} else {
			info = (cli_info *)malloc(sizeof(cli_info));	
			info->next = cli_info_head;
			cli_info_head = info;
			info->sock = ns;
			info->mode = HEADER;
			info->r_lasttime = now;
			info->w_lasttime = now;
			info->in_data_packet.next = NULL;
			info->in_data_packet.bytes = 8;
			info->in_data_packet.start_ptr = info->buff;
			info->in_data_packet.packet = NULL;
			info->out_data_head = NULL;
			info->out_data_tail = &(info->out_data_head);
		}
		
	}

	for (info = cli_info_head; info; info = info->next) {
		if (FD_ISSET(info->sock, rset) && info->mode != KILL) {
			info->r_lasttime = now;
			read_from_cli(info);
		}
		if (FD_ISSET(info->sock, wset) && info->mode != KILL) {
			info->w_lasttime = now;
//			write_to_cli(info);
		} 
	}

	p_info = &cli_info_head;
	while ((info = *p_info)) {
		if (info->mode == KILL) {
			tcpclose(info->sock);
			if (info->in_data_packet.packet) {
				free(info->in_data_packet.packet);
			}
			ptr = info->out_data_head;
			while (ptr) {
				if (ptr->packet) {
					free(ptr->packet);
				}
					patr = ptr;
					ptr = ptr->next;
					free(patr);
			}
			*p_info = info->next;
			free(info);	
		} else {
			p_info = &(info->next);
		}
	}
}

int connect_cli()
{
	int ret = 0;
	char *host = "127.0.0.1";
	char *port = "2015";

	exiting = 0;
	cli_info_head = NULL;

	lsock = tcpsocket();

	ret = tcplisten(lsock, host, port, 5);
	if (ret < 0) {
		fprintf(stderr, "listen cli error\n");
	}

	register_cli(cli_desc, cli_serv);	

	fprintf(stderr, "init drw complete, host:%s, port:%s\n", host, port);
	return 0;
}

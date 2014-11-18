#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ser.h"
#include "fs.h"
#include "sockets.h"
#include "data_pack.h"
#include "../communication.h"

#define MaxPacketSize 1000000
#define MAX_NAME_LEN 1024

char PATH[] = "/home/cc";


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

void cc_fuse_getattr(cli_info *info,uint8_t *data,uint32_t length) 
{
	uint32_t inode;
	uint8_t attr[35];
	uint32_t msgid;
	uint8_t *ptr;
	uint8_t status;
	if (length!=8) {
		fprintf(stderr,"CUTOMA_FUSE_GETATTR - wrong size (%d/8)",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	status = fs_getattr(inode,attr);
	
	ptr = create_cli_packet(info,MATOCU_FUSE_GETATTR,(status!=STATUS_OK)?5:4+35);
	if (ptr==NULL) {
		fprintf(stderr, "can't allocate memory for packet");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);
	if (status!=STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		memcpy(ptr,attr,35);
	}
}


void cc_fuse_getdir(cli_info *info,uint8_t *data,uint32_t length) 
{
	uint32_t inode,uid,gid;
	uint32_t msgid;
	uint8_t *ptr = NULL;
	uint8_t status;
	uint32_t len = 0;

	if (length!=16) {
		fprintf(stderr,"CUTOMA_FUSE_GETDIR - wrong size (%d/16)",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET32BIT(uid,data);
	GET32BIT(gid,data);
  	
	if (getdir_size(inode, &len) == 0) {
		status = STATUS_OK;
	} else {
		status = 3;
	}

	ptr = create_cli_packet(info,MATOCU_FUSE_GETDIR,(status!=STATUS_OK)?5:4+len);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);

	if (status!=STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		fs_getdir(inode, ptr);
	//	memcpy(ptr, buf, len);
	//	free(buf);
	}
}

void cc_fuse_lookup(cli_info *info,uint8_t *data,uint32_t length) {
	uint32_t inode,uid,gid;
	uint8_t nleng;
	uint8_t *name;
	uint32_t new_inode;
	uint8_t attr[35];
	uint32_t msgid;
	uint8_t *ptr;
	uint8_t status;
	
	if (length<17) {
		fprintf(stderr, "CUTOMA_FUSE_LOOKUP - wrong size (%d)\n",length);
			info->mode = KILL;
			return;
	}
	
	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET8BIT(nleng,data);
	
	if (length != 17U+nleng) {
		fprintf(stderr,"CUTOMA_FUSE_LOOKUP - wrong size (%d:nleng=%d)\n",length,nleng);
		info->mode = KILL;
		return;
	}
		
	name = data;
	data += nleng;
	GET32BIT(uid,data);
	GET32BIT(gid,data); 
	
	if (fs_lookup(inode, name, nleng, attr, &new_inode) == 0) {
		status= STATUS_OK;
	} else {
		status = 3;
	}

	ptr = create_cli_packet(info,MATOCU_FUSE_LOOKUP,(status!=STATUS_OK)?5:8+35);
	if (ptr==NULL) {
		fprintf(stderr, "can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);
	if (status!=STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		PUT32BIT(new_inode,ptr);
		memcpy(ptr,attr,35);
	}
}


void cc_fuse_statfs(cli_info *info,uint8_t *data,uint32_t length)
{
	uint64_t totalspace,availspace,trashspace,reservedspace;
	uint32_t msgid,inodes;
	uint8_t *ptr;

	if (length !=4 ) {
		fprintf(stderr,"CUTOMA_FUSE_STATFS - wrong size (%d/4)\n",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	fs_statfs(&totalspace,&availspace,&trashspace,&reservedspace,&inodes);

	ptr = create_cli_packet(info,MATOCU_FUSE_STATFS,40);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);
	PUT64BIT(totalspace,ptr);
	PUT64BIT(availspace,ptr);

	PUT64BIT(trashspace,ptr);
	PUT64BIT(reservedspace,ptr);
	PUT32BIT(inodes,ptr);
}

void cc_fuse_read_chunk(cli_info *info, uint8_t *data,uint32_t length) 
{
	uint8_t *ptr;
	uint8_t status = 0;
	uint32_t inode = 0;
	uint32_t indx = 0;
	uint64_t chunkid = 0;
	uint32_t ip = 0;
	uint16_t port = 0;
	uint32_t msgid = 0;
	uint64_t file_size;

	if (length!=12) {
		fprintf(stderr,"DRWTOSER_FUSE_READ_CHUNK - wrong size (%d/12)\n",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET32BIT(indx,data);

	status = fs_read_chunk(inode, indx, &chunkid, &file_size);

fprintf(stderr, "----------chunkid:%llu,size:%llu-----------\n", chunkid, file_size);
	if (status != STATUS_OK) {
		ptr = create_cli_packet(info,SERTODRW_FUSE_READ_CHUNK,5);
		if (ptr==NULL) {
			fprintf(stderr, "can't allocate memory for packet\n");
			info->mode = KILL;
			return;
		}
		PUT32BIT(msgid,ptr);
		PUT8BIT(status,ptr);
		return;
	}
	ptr = create_cli_packet(info,SERTODRW_FUSE_READ_CHUNK,26);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);
	PUT64BIT(chunkid,ptr);
	PUT64BIT(file_size,ptr);
	ip = 127*256*256*256+1;
	PUT32BIT(ip,ptr);
	port = 2015;
	PUT16BIT(port,ptr);
}

void cc_write_chunk(cli_info *info, uint8_t *data,uint32_t length) 
{
	uint8_t *ptr;
	uint32_t ip = 0;
	uint16_t port = 0;
	uint32_t msgid = 0;
	uint32_t indx = 0;
	uint64_t chunkid = 0;
	uint32_t inode = 0;
	int8_t status = 0;

	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET32BIT(indx,data);

	status = add_chunkid_to_fs(inode, indx, &chunkid);

	ptr = create_cli_packet(info,SERTODRW_WRITE_CHUNK,(status!=STATUS_OK)?5:18);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}

	PUT32BIT(msgid,ptr);
	if (status!=STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		PUT64BIT(chunkid, ptr);

		ip = 127*256*256*256+1;
		PUT32BIT(ip,ptr);
		port = 2015;
		PUT16BIT(port,ptr);
	}
}
	
void cc_write_chunk_end(cli_info *info, uint8_t *data,uint32_t length) 
{
	uint8_t *ptr;
	uint32_t msgid = 0;
	uint32_t indx = 0;
	uint32_t inode = 0;
	uint64_t size = 0;
	int8_t status = 0;

	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET32BIT(indx,data);
	GET64BIT(size,data);

	status = change_file_size(inode, indx, size);

	ptr = create_cli_packet(info,SERTODRW_WRITE_CHUNK_END,5);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}

	PUT32BIT(msgid,ptr);
	PUT8BIT(status,ptr);
}
	
void cc_fs_getdir(cli_info *info,uint8_t *data,uint32_t length) 
{
	uint32_t inode,uid,gid;
	uint32_t msgid;
	uint8_t *ptr = NULL;
	uint8_t status;
	uint32_t len = 0;

	if (length!=16) {
		fprintf(stderr,"SERTODRW_FS_GETDIR - wrong size (%d/16)",length);
		info->mode = KILL;
		return;
	}
	GET32BIT(msgid,data);
	GET32BIT(inode,data);
	GET32BIT(uid,data);
	GET32BIT(gid,data);
  	
	if (getdir_size_from_drw(inode, &len) == 0) {
		status = STATUS_OK;
	} else {
		status = 3;
	}

	ptr = create_cli_packet(info,SERTODRW_FS_GETDIR,(status!=STATUS_OK)?5:4+len);
	if (ptr==NULL) {
		fprintf(stderr,"can't allocate memory for packet");
		info->mode = KILL;
		return;
	}
	PUT32BIT(msgid,ptr);

	if (status!=STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		fs_getdir_from_drw(inode, ptr);
	}
}


void cc_judge_name(cli_info *info,uint8_t *data,uint32_t length)
{
	uint8_t path_len;
	uint8_t *name = NULL;
	uint32_t new_inode;
	uint8_t *ptr = NULL;
	uint8_t status;
	uint32_t msgid;	
	
	GET32BIT(msgid,data);
	GET16BIT(path_len,data);
		
	name = data;
	data += path_len;
	
	if (fs_look_name(name, path_len, &new_inode) == 0) {
		status= STATUS_OK;
	} else {
		status = 3;
	}

	ptr = create_cli_packet(info, MATODRW_JUDGE_NAME, (status!=STATUS_OK)?5:8);
	if (ptr==NULL) {
		fprintf(stderr, "can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}

	PUT32BIT(msgid,ptr);
	if (status != STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		PUT32BIT(new_inode,ptr);
	}
}


void cc_creat_file(cli_info *info,uint8_t *data,uint32_t length)
{
	uint8_t path_len;
	uint8_t *name = NULL;
	uint32_t new_inode;
	uint8_t *ptr = NULL;
	uint8_t status;
	uint32_t msgid;	
	
	GET32BIT(msgid,data);
	GET16BIT(path_len,data);
		
	name = data;
	data += path_len;
	
	if (fs_creat_file(name, path_len, &new_inode) == 0) {
		status= STATUS_OK;
	} else {
		status = 3;
	}

	ptr = create_cli_packet(info, SERTODRW_CREAT_FILE, (status!=STATUS_OK)?5:8);
	if (ptr==NULL) {
		fprintf(stderr, "can't allocate memory for packet\n");
		info->mode = KILL;
		return;
	}

	PUT32BIT(msgid,ptr);
	if (status != STATUS_OK) {
		PUT8BIT(status,ptr);
	} else {
		PUT32BIT(new_inode,ptr);
	}
}

void analyze_cli_packet(cli_info *info,uint32_t type,uint8_t *data,uint32_t length) 
{
	switch (type) {
		case ANTOAN_NOP:
			break;
		case CUTOMA_FUSE_GETATTR:
			cc_fuse_getattr(info,data,length);
			break;
		case CUTOMA_FUSE_GETDIR:
			cc_fuse_getdir(info,data,length);
			break;
		case CUTOMA_FUSE_LOOKUP:
			cc_fuse_lookup(info,data,length);
			break;
		case CUTOMA_FUSE_STATFS:
			cc_fuse_statfs(info,data,length);
			break;

// --- drw <-> ser
		case DRWTOSER_FUSE_READ_CHUNK:
			cc_fuse_read_chunk(info,data,length);
			break;
		case DRWTOSER_FS_GETDIR:
			cc_fs_getdir(info,data,length);
			break;
		case DRWTOMA_JUDGE_NAME:
			cc_judge_name(info,data,length);
			break;
		case DRWTOSER_CREAT_FILE:
			cc_creat_file(info, data, length);
			break;
		case DRWTOSER_WRITE_CHUNK:
			cc_write_chunk(info,data,length);
			break;
		case DRWTOSER_WRITE_CHUNK_END:
			cc_write_chunk_end(info,data,length);
			break;
		default:
			info->mode = KILL;
			fprintf(stderr, "not recognition type(%d)\n", type);

	
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
if (type) {
	fprintf(stderr, "cli read, size %d mode: %d ", info->in_data_packet.bytes, info->mode);
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
			write_to_cli(info);
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
	char *port = "2014";

	exiting = 0;
	cli_info_head = NULL;

	lsock = tcpsocket();

	ret = tcplisten(lsock, host, port, 5);
	if (ret < 0) {
		fprintf(stderr, "listen cli error\n");
	}

	register_cli(cli_desc, cli_serv);	

	fprintf(stderr, "init ser complete\n");
	return 0;
}

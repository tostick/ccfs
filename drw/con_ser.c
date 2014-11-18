#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "sockets.h"
#include "data_pack.h"
#include "../communication.h"

#define VERSMAJ 1
#define VERSMID 3
#define VERSMIN 2

#define DEFAULT_BUFFSIZE 10000
#define RECEIVE_TIMEOUT 10
#define RETRIES 30
#define MAX_NAME_LEN 1024


static int fd;
static int disconnect;
//static time_t w_lasttime;

static char *ip;
static char *port;
static uint32_t cuid;

static pthread_t rthid;

static pthread_mutex_t fdlock;

typedef struct _threc {
	pthread_t thid;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	uint8_t *buff;
	uint32_t buffsize;
	uint8_t sent;
	uint8_t status;
	uint8_t release;
	uint32_t size;
	uint32_t cmd;
	uint32_t packetid;
	struct _threc *next;
} threc;

static threc *threc_head = NULL;

void fs_buffer_init(threc *rec,uint32_t size) {
	if (size>DEFAULT_BUFFSIZE) {
		rec->buff = realloc(rec->buff,size);
		rec->buffsize = size;
	} else if (rec->buffsize>DEFAULT_BUFFSIZE) {
		rec->buff = realloc(rec->buff,DEFAULT_BUFFSIZE);
		rec->buffsize = DEFAULT_BUFFSIZE;
	}
}

threc* fs_get_threc_by_id(uint32_t packetid) {
	threc *rec;
	for (rec = threc_head ; rec ; rec=rec->next) {
		if (rec->packetid==packetid) {
			return rec;
		}
	}
	return NULL;
}

uint8_t* fs_createpacket(threc *rec,uint32_t cmd,uint32_t size) {
	uint8_t *ptr;
	uint32_t hdrsize = size+4;
	fs_buffer_init(rec,size+12);
	if (rec->buff==NULL) {
		return NULL;
	}
	ptr = rec->buff;
	PUT32BIT(cmd,ptr);
	PUT32BIT(hdrsize,ptr);
	PUT32BIT(rec->packetid,ptr);
	rec->size = size+12;

	return rec->buff+12;
}

uint8_t* fs_sendandreceive(threc *rec,uint32_t command_info,uint32_t *info_length) {
	uint32_t cnt;
	uint32_t size = rec->size;

	for (cnt=0 ; cnt<RETRIES ; cnt++) {
		pthread_mutex_lock(&fdlock);
		if (fd==-1) {
			pthread_mutex_unlock(&fdlock);
			sleep(1);
			continue;
		}
		rec->release=0;
if (DEBUG) {	
int i = 0;
fprintf(stderr, "tcptowrite buf:%s, size:%d, cmd:%d\n", rec->buff, size, command_info);
for(i = 0; i< size; i++) {
	printf("%u-", rec->buff[i]);
}
printf("\n");
}
		if (tcptowrite(fd,rec->buff,size,1000)!=(int32_t)(size)) {
			fprintf(stderr, "tcp send error\n");
			disconnect = 1;
			pthread_mutex_unlock(&fdlock);
			sleep(1);
			continue;
		}
		rec->sent = 1;
//		last_write = time(NULL);
		pthread_mutex_unlock(&fdlock);
		pthread_mutex_lock(&(rec->mutex));
		while (rec->release==0) { pthread_cond_wait(&(rec->cond),&(rec->mutex)); }
		pthread_mutex_unlock(&(rec->mutex));
		if (rec->status!=0) {
			sleep(1);
			continue;
		}
		if (rec->cmd!=command_info) {
			pthread_mutex_lock(&fdlock);
			disconnect = 1;
			pthread_mutex_unlock(&fdlock);
			sleep(1);
			continue;
		}
		*info_length = rec->size;
		return rec->buff+size;
	}
	return NULL;
}


threc* fs_get_my_threc() {
	pthread_t mythid = pthread_self();
	threc *rec;
	for (rec = threc_head ; rec ; rec=rec->next) {
		if (pthread_equal(rec->thid,mythid)) {
			return rec;
		}
	}
	rec = malloc(sizeof(threc));
	rec->thid = mythid;
	if (threc_head==NULL) {
		rec->packetid = 1;
	} else {
		rec->packetid = threc_head->packetid+1;
	}
	pthread_mutex_init(&(rec->mutex),NULL);
	pthread_cond_init(&(rec->cond),NULL);
	rec->buff = malloc(DEFAULT_BUFFSIZE);
	if (rec->buff==NULL) {
		free(rec);
		return NULL;
	}
	rec->buffsize = DEFAULT_BUFFSIZE;
	rec->sent = 0;
	rec->status = 0;
	rec->release = 0;
	rec->cmd = 0;
	rec->size = 0;
	rec->next = threc_head;
	threc_head = rec;
	return rec;
}


uint8_t fs_getattr(uint32_t inode,uint8_t attr[35]) {
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;
	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,CUTOMA_FUSE_GETATTR,4);
	PUT32BIT(inode,ptr);
	ptr = fs_sendandreceive(rec,MATOCU_FUSE_GETATTR,&i);
	if (ptr==NULL) {
		ret = ERROR_IO;
	} else if (i==1) {
		ret = ptr[0];
	} else if (i!=35) {
		pthread_mutex_lock(&fdlock);
		disconnect = 1;
		pthread_mutex_unlock(&fdlock);
		ret = ERROR_IO;
	} else {
		memcpy(attr,ptr,35);
		ret = STATUS_OK;
	}

	return ret;
}

uint8_t fs_getdir(uint32_t inode,uint32_t uid,uint32_t gid,uint8_t **dbuff,uint32_t *dbuffsize) {
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;
	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,CUTOMA_FUSE_GETDIR,12);
	PUT32BIT(inode,ptr);
	PUT32BIT(uid,ptr);
	PUT32BIT(gid,ptr);
	ptr = fs_sendandreceive(rec,MATOCU_FUSE_GETDIR,&i);
	if (ptr==NULL) {
		ret = ERROR_IO;
	} else if (i==1) {
		ret = ptr[0];
	} else {
		*dbuff = ptr;
		*dbuffsize = i;
		ret = STATUS_OK;
	}
	return ret;
}

uint8_t fs_lookup(uint32_t parent,uint8_t nleng,const uint8_t *name,uint32_t uid,uint32_t gid,uint32_t *inode,uint8_t attr[35]) {
	uint8_t *ptr;
	uint32_t i;
	uint32_t t32;
	uint8_t ret;
	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,CUTOMA_FUSE_LOOKUP,13+nleng);
	PUT32BIT(parent,ptr);
	PUT8BIT(nleng,ptr);
	memcpy(ptr,name,nleng);
	ptr+=nleng;
	PUT32BIT(uid,ptr);
	PUT32BIT(gid,ptr);
	ptr = fs_sendandreceive(rec,MATOCU_FUSE_LOOKUP,&i);
	if (ptr==NULL) {
		ret = ERROR_IO;
	} else if (i==1) {
		ret = ptr[0];
	} else if (i!=39) {
		pthread_mutex_lock(&fdlock);
		disconnect = 1;
		pthread_mutex_unlock(&fdlock);
		ret = ERROR_IO;
	} else {
		GET32BIT(t32,ptr);
		*inode = t32;
		memcpy(attr,ptr,35);
		ret = STATUS_OK;
	}
	return ret;
}

void fs_statfs(uint64_t *totalspace,uint64_t *availspace,uint64_t *trashspace,
		uint64_t *reservedspace,uint32_t *inodes) 
{
	uint64_t t64;
	uint32_t t32;
	uint8_t *ptr;
	uint32_t i;
	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,CUTOMA_FUSE_STATFS,0);
	ptr = fs_sendandreceive(rec,MATOCU_FUSE_STATFS,&i);
	if (ptr==NULL || i!=36) {
		*totalspace = 0;
		*availspace = 0;
		*trashspace = 0;
		*reservedspace = 0;
		*inodes = 0;
	} else {
		GET64BIT(t64,ptr);
		*totalspace = t64;
		GET64BIT(t64,ptr);
		*availspace = t64;
		GET64BIT(t64,ptr);
		*trashspace = t64;
		GET64BIT(t64,ptr);
		*reservedspace = t64;
		GET32BIT(t32,ptr);
		*inodes = t32;
	}
}

uint8_t fs_readchunk(uint32_t inode,uint32_t indx,uint64_t *chunkid,
				uint64_t *size, uint32_t *ip,uint16_t *port) 
{
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;

	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,DRWTOSER_FUSE_READ_CHUNK,8);
	PUT32BIT(inode,ptr);
	PUT32BIT(indx,ptr);
	ptr = fs_sendandreceive(rec,SERTODRW_FUSE_READ_CHUNK,&i);

	if (ptr==NULL || i != 22) {
		ret = ERROR_IO;
	} else {
		GET64BIT(*chunkid,ptr);
		GET64BIT(*size,ptr);
		GET32BIT(*ip,ptr);
		GET16BIT(*port,ptr);

		ret = STATUS_OK;
	}
	return ret;
}

uint8_t fs_writechunk(uint32_t *ip,uint16_t *port, uint64_t *chunkid,
				uint32_t inode, uint32_t indx) 
{
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;

	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,DRWTOSER_WRITE_CHUNK,8);
	PUT32BIT(inode,ptr);
	PUT32BIT(indx,ptr);
	ptr = fs_sendandreceive(rec,SERTODRW_WRITE_CHUNK,&i);

	if (ptr==NULL || i != 14) {
		ret = ERROR_IO;
	} else {
		GET64BIT(*chunkid, ptr);
		GET32BIT(*ip,ptr);
		GET16BIT(*port,ptr);

		ret = STATUS_OK;
	}
	return ret;
}


uint8_t fs_writechunk_end(uint32_t inode, uint32_t indx, uint64_t size) 
{
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;

	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,DRWTOSER_WRITE_CHUNK_END,16);
	PUT32BIT(inode,ptr);
	PUT32BIT(indx,ptr);
	PUT64BIT(size,ptr);
	ptr = fs_sendandreceive(rec,SERTODRW_WRITE_CHUNK_END,&i);

	if (ptr==NULL || i != 2) {
		ret = ERROR_IO;
	} else {
		GET16BIT(ret, ptr);
	}
	return ret;
} 


uint8_t fs_info(uint32_t inode,uint32_t uid,uint32_t gid,uint8_t **dbuff,uint32_t *dbuffsize) 
{
	uint8_t *ptr;
	uint32_t i;
	uint8_t ret;
	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,DRWTOSER_FS_GETDIR, 12);
	PUT32BIT(inode,ptr);
	PUT32BIT(uid,ptr);
	PUT32BIT(gid,ptr);
	ptr = fs_sendandreceive(rec,SERTODRW_FS_GETDIR,&i);
	if (ptr==NULL) {
		ret = ERROR_IO;
	} else if (i==1) {
		ret = ptr[0];
	} else {
		*dbuff = ptr;
		*dbuffsize = i;
		ret = STATUS_OK;
	}
	return ret;
}


uint8_t fs_judge_name(uint16_t path_len, uint8_t *name,uint32_t *inode)
{
	uint8_t *ptr;
	uint32_t i;
	uint32_t t32;

	threc *rec = fs_get_my_threc();
	ptr = fs_createpacket(rec,DRWTOMA_JUDGE_NAME,2+path_len);
	PUT16BIT(path_len, ptr);
	memcpy(ptr, name, path_len);
	ptr += path_len;
	ptr = fs_sendandreceive(rec,MATODRW_JUDGE_NAME,&i);
	if (ptr==NULL) {
		return 3;
	} 
	
	if (i == 4) {
		GET32BIT(t32,ptr);
		*inode = t32;
		return 0;
	}
	return 3;
}

uint8_t fs_creat_file(uint16_t path_len, uint8_t *name,uint32_t *inode)
{
	uint8_t *ptr;
	uint32_t i;
	uint32_t t32;

	threc *rec = fs_get_my_threc();
// 权限默认644，大小为0，类型为文件
	ptr = fs_createpacket(rec,DRWTOSER_CREAT_FILE,2+path_len);
	PUT16BIT(path_len, ptr);
	memcpy(ptr, name, path_len);
	ptr += path_len;
	ptr = fs_sendandreceive(rec,SERTODRW_CREAT_FILE,&i);
	if (ptr==NULL) {
		return 3;
	} 
	
	if (i == 4) {
		GET32BIT(t32,ptr);
		*inode = t32;
		return 0;
	}
	return 3;
}


void connect_ser()
{
	fd = tcpsocket();

	if (tcpconnect(fd, ip, port) < 0) {
		fprintf(stderr, "can't connect to ser(ip:%s,port:%s)\n", ip, port);
		tcpclose(fd);
		fd = -1;
		return;
	} else {
		fprintf(stderr, "connect to ser(ip:%s,port:%s)\n", ip, port);
		disconnect = 0;
	}

}

void *receive_thread(void *arg)
{
	uint8_t *ptr = NULL;
	uint8_t hdr[12] = {0};
	uint32_t cmd = 0;
	uint32_t size = 0;
	uint32_t packetid = 0;
	int r = 0;
	threc *rec = NULL;


	for (;;) {
		if (fd == -1 && disconnect) {
			connect_ser();
		}

		if (disconnect) {
			tcpclose(fd);
			fd = -1;
			
			for (rec = threc_head; rec; rec=rec->next) {
			
			}
		}	
	
		if (fd == -1) {
			fprintf(stderr, "reconnect ser(ip:%s,port:%s)\n", ip, port);
			sleep(2);
			continue;
		}	
		
//		r = tcptoread(fd,hdr,12,RECEIVE_TIMEOUT*1000);	// read timeout - 4 seconds

		r = read(fd,hdr,12);

		if (r==0) {
			fprintf(stderr, "ser: connection lost (1)\n");
			disconnect=1;
			continue;
		}
		if (r!=12) {
			fprintf(stderr,"ser: tcp recv error(1), %d\n", r);
			disconnect=1;
			continue;
		}
		
		ptr = hdr;
		GET32BIT(cmd,ptr);
		GET32BIT(size,ptr);
		GET32BIT(packetid,ptr);
if (DEBUG) {
	fprintf(stderr, "read, cmd:%u\n", cmd);
	fprintf(stderr, "read, size:%u\n", size);
	fprintf(stderr, "read, packetid:%u\n", packetid);
}
		if (cmd==ANTOAN_NOP && size==4) {
			// syslog(LOG_NOTICE,"master: got nop");
			continue;
		}
		if (size<4) {
			fprintf(stderr,"ser: packet too small\n");
			disconnect=1;
			continue;
		}
		size-=4;
		rec = fs_get_threc_by_id(packetid);
		if (rec==NULL) {
			fprintf(stderr, "ser: get unexpected queryid\n");
			disconnect=1;
			continue;
		}
		fs_buffer_init(rec,rec->size+size);
		if (rec->buff==NULL) {
			disconnect=1;
			continue;
		}
		// syslog(LOG_NOTICE,"master: expected data size: %u",size);
		if (size>0) {
			r = tcptoread(fd,rec->buff+rec->size,size,1000);
if (DEBUG) {
int i;
fprintf(stderr, "read buf:%s, size:%d\n", rec->buff, size);
for(i = 0; i< size; i++) {
	fprintf(stderr, "%u-", rec->buff[rec->size+i]);
}
fprintf(stderr, "\n"); 
}

			// syslog(LOG_NOTICE,"master: data size: %u",r);
			if (r==0) {
				fprintf(stderr,"ser: connection lost (2)\n");
				disconnect=1;
				continue;
			}
			if (r!=(int32_t)(size)) {
				fprintf(stderr,"ser: tcp recv error(2)\n");
				disconnect=1;
				continue;
			}
		}
		rec->sent=0;
		rec->status=0;
		rec->size = size;
		rec->cmd = cmd;
		pthread_mutex_lock(&(rec->mutex));
		rec->release = 1;
		pthread_mutex_unlock(&(rec->mutex));
		pthread_cond_signal(&(rec->cond));
	}
}

void ser_init(char *_ip, char *_port)
{
	ip = strdup(_ip);
	port = strdup(_port);
	cuid = 0;
	fd = -1;
	disconnect = 1;	
	
	pthread_mutex_init(&fdlock,NULL);
	pthread_create(&rthid, NULL, receive_thread, NULL);
}

void close_service()
{
	fd = -1;
	tcpclose(fd);
}



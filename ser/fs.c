#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include "data_pack.h"
#include "chunks.h"
#include "../communication.h"
#include "fs.h"

#define NODE_HASH_SIZE (1<<16)
#define NODE_HASH_POS(id) ((id)&(NODE_HASH_SIZE-1))

#define PATH "/home/cc"
#define CONFIG "/opt/config"
#define MAX_NAME_LEN 1024
#define MAX_PATH_LEN 1024
#define MAX_STR_LEN 1024

static uint32_t root_inode = 10747905;
static uint32_t file_num = 0;
static uint32_t inode_next = 100;

extern uint64_t next_chunkid;

struct _fs_node;
typedef struct _fs_edge {
	struct _fs_node **child;  // 
	struct _fs_node *parent;
	uint8_t nleng;
	uint8_t *name;
	uint64_t size;
	uint32_t n_child; // 孩子的数量
}fs_edge;

typedef struct _fs_node {
	uint32_t inode;
	uint8_t type;
	
	uint64_t *chunk_tab;
	uint32_t chunks;

	fs_edge *self;
	struct _fs_node *next;  // next 代表同一位置的节点
}fs_node;

static fs_node *node_hash[NODE_HASH_SIZE];
static uint8_t change = 0;
int8_t save_file_to_config(char *name, uint32_t inode, uint8_t type, uint64_t size);

fs_node * find_fsnode_by_inode(uint32_t inode)
{
	uint32_t pos = -1;
	fs_node *p;

	pos = NODE_HASH_POS(inode);
	p = node_hash[pos];

	while (p) {
		if(p->inode == inode) {
			return p;
		}
		p = p->next;
	}
	
	return NULL;
}

int8_t save_fs_node(char *name, uint32_t inode, uint8_t type, uint64_t size, 
				uint32_t parent_inode, uint32_t chunks, uint64_t *chunk_tab)
{

	uint32_t pos = -1;
	uint8_t i = 0;
	fs_node *p = NULL;

	p = malloc(sizeof(fs_node));
	if (p == NULL) {
		fprintf(stderr, "malloc p failed\n");
		return 1;
	}

	file_num ++;

	p->inode = inode;
	p->type = type;
	if (type == 102) {
		p->chunks = chunks;
		if (chunks > 0) {
			p->chunk_tab = malloc(sizeof(uint64_t)*chunks);
			for (i = 0; i < chunks; i++) {
				p->chunk_tab[i] = chunk_tab[i];
			}
		}
	} else {
		p->chunks = 0;
	}
	p->next = NULL;

	p->self = malloc(sizeof(fs_edge));
	p->self->nleng = strlen(name);
	p->self->name = malloc(sizeof(uint8_t)*p->self->nleng);
	memcpy(p->self->name, name, p->self->nleng);
	p->self->size = size;

	// 新加入的孩子没有孩子
	p->self->n_child = 0;
	p->self->child = NULL; // 没有赋空，导致realloc error

	pos = NODE_HASH_POS(inode);
	if (node_hash[pos] == NULL) {
		node_hash[pos] = p;
	} else {
		p->next = node_hash[pos]; // insert in link head
		node_hash[pos] = p;
	}
 // 该节点为根节点或者该节点为主节点
	if (inode == root_inode || inode == parent_inode) { 
		p->self->parent = NULL;
		return 0;
	}

	// 获取父节点
	fs_node *parent = NULL;
	parent = find_fsnode_by_inode(parent_inode);
	if (parent == NULL) {
		fprintf(stderr, "-----not find parent ----\n");
		return 1;
	}

// 父亲的孩子
	if (parent->self->n_child % 100 == 0) {
	  parent->self->child = realloc(parent->self->child, (parent->self->n_child+100)*sizeof(fs_node *));

	  if (parent->self->child == NULL) {
		  fprintf(stderr, "realloc failed\n");
		  return 1;
	  } 
	} 
	parent->self->child[parent->self->n_child] = p;
	parent->self->n_child ++;

// 孩子的父亲，如果需要祖父，则必须找父亲的父亲
	p->self->parent = parent;  

	return 0;

}

int load_info(char *path);
int refresh_info() {
	return 0;
}

void list_all_child(uint32_t inode)
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	char name[MAX_NAME_LEN] = {0};
	uint8_t i = 0;

	p = find_fsnode_by_inode(inode);

	if (p == NULL) {
		fprintf(stderr, "not find current inode\n");
		return;
	}

	// print current inode
	memcpy(name, p->self->name, p->self->nleng);
	fprintf(stderr, "current:%s (%d-%d)\n", name, p->self->n_child, p->self->nleng);

	if (inode != root_inode) {
		p1 = p->self->parent;
		memset(name, 0, sizeof(name));
		memcpy(name, p1->self->name, p1->self->nleng);
//		fprintf(stderr, "parent:%s\n", name);
	}

	if (p->self->n_child == 0) {
//		fprintf(stderr, "no children\n");
		return;
	}

	for (i = 0; i < p->self->n_child; i++) {
		memset(name, 0, sizeof(name));
		p1 = p->self->child[i];
		memcpy(name, p1->self->name, p1->self->nleng);
//		fprintf(stderr, "child:%s\n\n", name);

		if (p->type == 100) {
			list_all_child(p1->inode);
		}
	}
		
}

void init_file_info()
{
	uint32_t i = 0;

	for(i = 0; i < NODE_HASH_SIZE; i++) {
		node_hash[i] = NULL;
	}
}

void trim_blank(char *str)
{
	size_t len = strlen(str);
	size_t lws;

	while(len && isspace(str[len-1]))
	{ --len; }

	if(len)
	{
		lws = strspn(str, "\n\r\t\v ");
		memmove(str, str + lws, len -= lws);
	}
	str[len] = 0;
	

	return;
}

/* buffer 待查找的字符 col 选取哪一段内容 1,2，3...
 * delim 查找条件 result 查找结果 len result的总长度
 * 在字符串中根据指定字符获取指定段的内容*/
int get_buffer_col(const char *buffer, const int col, const char delim, char *result, int len)
{
	char *pos, *beg;
	int cp_len = 0, num = 0;
	char *buf_tmp = strdup(buffer);
	trim_blank(buf_tmp); // 去除空格，避免以空格作为分割符时，行首行尾空格对结果造成影响

	beg = buf_tmp;

	while(1)
	{
		pos = strchr(beg, delim);
		num ++;
		if(pos == NULL || num == col)
		{
			break;
		}
		beg = pos + 1;
		if(delim == ' ')
		{
			while(*beg == delim)
			{ beg++;} // 考虑字符之间多个 空格的 情况
		}
	}

	if(num == col)
	{
		// pos 判断是否在剩余字符串中找到delim, 没有则获取剩下的字符
		cp_len = (pos)? (pos - beg):(buf_tmp + strlen(buf_tmp) - beg);
		memset(result, 0, len);	
		strncpy(result, beg, (len > cp_len)?cp_len : len);
		trim_blank(result);
	}

	
	free(buf_tmp);
	return 0;
}

uint32_t char_to_uint32(const char *buf)
{
	uint32_t val = 0;
	uint8_t i = 0;

	while (buf[i]) {
		val *= 10;
		val += (buf[i++] - '0');
	}

	return val;
}

uint64_t char_to_uint64(const char *buf)
{
	uint64_t val = 0;
	uint8_t i = 0;

	while (buf[i]) {
		val *= 10;
		val += (buf[i++] - '0');
	}

	return val;
}

//int main(int argc, char **argv) 
int8_t load_config(uint32_t inode)
{
	char path[MAX_PATH_LEN] = {0};
	char buf[MAX_STR_LEN] = {0};
	char result[MAX_STR_LEN] = {0};
	char name[MAX_NAME_LEN] = {0};
	uint32_t count = 0;
	FILE *fp = NULL;
	uint32_t sub_inode;
	uint8_t type;
	uint64_t size;
	uint32_t chunks;
	uint32_t num_chunks;
	uint64_t *chunk_tab = NULL;
	uint32_t index = 0;

	sprintf(path, "%s/%u", CONFIG, (unsigned int)inode);
	fp = fopen(path, "a+");
	if (fp == NULL) {
		fprintf(stderr, "open file(%s) failed\n", path);
		return -1;
	}
	
	inode_next = root_inode;
	while(fgets(buf, sizeof(buf), fp) != NULL) {
		fprintf (stderr, "%s\n", buf);
		count ++;
		get_buffer_col(buf,1,'$',result,sizeof(result));
		memcpy(name, result, strlen(result)+1);
		memset(result, 0, sizeof(result));
		
		get_buffer_col(buf,2,'$',result,sizeof(result));
		sub_inode = char_to_uint32(result);
		memset(result, 0, sizeof(result));

		get_buffer_col(buf,3,'$',result,sizeof(result));
		type = char_to_uint32(result);
		memset(result, 0, sizeof(result));
		
		get_buffer_col(buf,4,'$',result,sizeof(result));
		size = char_to_uint64(result);
		memset(result, 0, sizeof(result));

		get_buffer_col(buf,5,'$',result,sizeof(result));
		chunks = char_to_uint32(result);
		memset(result, 0, sizeof(result));
	printf("chunks: %d\n", chunks);

		if (chunks > 0) {
			chunk_tab = malloc(sizeof(uint64_t)*chunks);
			num_chunks = chunks;
			while (num_chunks--) {
				index = 5;
				index++;
				get_buffer_col(buf,index,'$',result,sizeof(result));
				chunk_tab[index-6] = char_to_uint64(result);
				if (chunk_tab[index-6] > next_chunkid) {
					next_chunkid = chunk_tab[index-6];
				}
				memset(result, 0, sizeof(result));
			}	
		}

		save_fs_node(name, sub_inode, type, size, inode, chunks, chunk_tab);
		if (chunks > 0) {
			free(chunk_tab);
		}
		if (sub_inode > inode_next) {
			inode_next = sub_inode;
		}
		memset(buf, 0, sizeof(buf));
	}

	fclose(fp);
	fprintf(stderr, "load %u objects\n", count);

	return 0;
}

int8_t load_fs()
{
	struct stat cstat = {0};
	char path[MAX_NAME_LEN] = {0};

	int ret = 0;

	init_file_info();

	sprintf(path, "%s", PATH);
	
	ret = stat(path, &cstat);
	if (ret != 0) {
		fprintf(stderr, "stat %s failed\n", path);
		return 1;
	}
	root_inode = cstat.st_ino;

	load_config(root_inode); // 每次导入一个文件夹的配置，其它的文件夹动态导入，节省节点占用内存大小
//	check_fs(); // 判断数据是否发生改变，使用crc校验

	fprintf(stderr, "file or folder count: %d\n", file_num);

	list_all_child(root_inode);
	return 0;
}

int get_attr_by_inode(uint32_t inode,uint8_t attr[35])
{
        int res = 1;
        char name[MAX_NAME_LEN] = {0};
        uint8_t temp[35] = {0};
	struct stat cstat = {0};
	fs_node *p = NULL;

// 不用担心出现. 和 .. , 因为这两种情况前面已经考虑完毕
	p = find_fsnode_by_inode(inode);

	if (p == NULL) {
		fprintf(stderr, "not find current inode\n");
		return 3;
	}

	memcpy(name, p->self->name, p->self->nleng);
        
        res = stat(name, &cstat);
        if (res != 0) {
            fprintf(stderr, "stat %s failed\n", name);
        } else {
            uint8_t *ptr;
            ptr = temp;
            PUT8BIT(p->type, ptr);
            PUT16BIT(cstat.st_mode, ptr);
            PUT32BIT(cstat.st_uid, ptr);
            PUT32BIT(cstat.st_gid, ptr);
            PUT32BIT(cstat.st_atime, ptr);
            PUT32BIT(cstat.st_mtime, ptr);
            PUT32BIT(cstat.st_ctime, ptr);
            PUT32BIT(cstat.st_nlink, ptr);
            PUT64BIT(cstat.st_size, ptr);
            memcpy(attr, temp, 35);
            res = 0;
        }
        return res;
}

int8_t fs_getattr(uint32_t inode,uint8_t attr[35]) 
{
	if (inode == 1) {
		inode = root_inode;
	}
        memset(attr,0,35);
        if (get_attr_by_inode(inode, attr) != 0) {
              return 3;
        }

        return 0;
}

int8_t fs_lookup(uint32_t inode, uint8_t *name, uint16_t nleng, uint8_t attr[35], uint32_t *child_inode) 
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	uint32_t i = 0;
	uint16_t len_parent = 0;

	if (inode == 1) {
		inode = root_inode;
	}

	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	} 
	
	if (name[0]=='.') {
		if (nleng == 1) {	// self
			*child_inode = inode;
        		if (get_attr_by_inode(*child_inode, attr) != 0) {
              			return 3;
        		}
			return 0;
		}
		if (nleng == 2 && name[1]=='.') {	// parent
			if (inode == root_inode) { // root
				*child_inode = inode;
			} else {
				*child_inode = p->self->parent->inode; 
			}
        		if (get_attr_by_inode(*child_inode, attr) != 0) {
              			return 3;
        		}
			return 0;
		}
	}
	
	// 找到孩子，并通过inode获取属性
	len_parent = p->self->nleng;
	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		if ((p1->self->nleng - len_parent-1) == nleng) {
			if (memcmp(&p1->self->name[len_parent+1], name, nleng) == 0 ) {
        			memset(attr,0,35);
				*child_inode = p1->inode;
        			if (get_attr_by_inode(*child_inode, attr) != 0) {
              				return 3;
        			}
				return 0;
			}
		}
	
	}

	return 3;
}


int8_t fs_look_name(uint8_t *name, uint16_t len, uint32_t *new_inode) 
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	int32_t i = 0;
	uint16_t name_len = 0;


	// 目前只考虑根目录
	p = find_fsnode_by_inode(root_inode);
	if (p == NULL) {
		return 3;
	} 
	
	// 获取文件名
	char sub_name[MAX_NAME_LEN] = {0};
	uint32_t pos = len; // 防止为文件夹的情况
	
    i = len - 1;
	while (i >= 0) {
		if (name[i] == '/') {
			break;
		}
		pos = i;
		i --;
	}

	name_len = len - pos;
	memcpy(sub_name, &name[pos], name_len);

	// 找到孩子，并通过inode获取属性
	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		if (p1->self->nleng == name_len) {
			if (memcmp(p1->self->name, &name[pos], name_len) == 0 ) {
				*new_inode = p1->inode;
				return 0;
			}
		}
	
	}

	fprintf(stderr, "object(%s) is unexist\n", sub_name);
		

	return 3;
}

int8_t fs_creat_file(uint8_t *name, uint16_t len, uint32_t *new_inode) 
{
	char sub_name[MAX_NAME_LEN] = {0};
	uint32_t pos = len; 
	uint16_t name_len = 0;
	int32_t i = 0;
	int8_t status = 0;
	
    i = len - 1;
	while (i >= 0) {
		if (name[i] == '/') {
			break;
		}
		pos = i;
		i --;
	}

	name_len = len - pos;
	memcpy(sub_name, &name[pos], name_len);

	inode_next ++;
	*new_inode = inode_next;
	// type:102 size:0
	status = save_fs_node(sub_name, *new_inode, 102, 0, root_inode,0, NULL);	
	if (status == 1) {
		fprintf(stderr, "save_fs_node failed inode is %d\n", *new_inode);
		return status;
	}
	status = save_file_to_config(sub_name, *new_inode, 102, 0);
	return status;
}

int8_t getdir_size(uint32_t inode, uint32_t *len)
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	uint32_t i = 0;
	uint16_t len_parent = 0;

	if (inode == 1) {
		inode = root_inode;
	}

	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	} 
	
	len_parent = p->self->nleng;
	*len = 6*2 + 3; // . and ..

	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		*len += (6 + p1->self->nleng - len_parent -1);  // -1 is dir /
	}
	
	return 0;
}	


int8_t getdir_size_from_drw(uint32_t inode, uint32_t *len)
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	uint32_t i = 0;
	uint16_t len_parent = 0;

	if (inode == 1) {
		inode = root_inode;
	}

	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	} 
	
	len_parent = p->self->nleng;

	*len = 0;
	// 14 = nleng + inode + type +size
	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		*len += (14 + p1->self->nleng); 
	}
	
	return 0;
}	

int8_t fs_getdir(uint32_t inode, uint8_t *name_info) 
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	uint32_t i = 0;
	uint16_t len_parent = 0;
	uint8_t *ptr = NULL;

	if (inode == 1) {
		inode = root_inode;
	}

	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	} 
	
	len_parent = p->self->nleng;

	ptr = name_info;
	ptr[0] = 1; // ptr指针在移动
	ptr[1] = '.';
	ptr += 2;
	PUT32BIT(p->inode, ptr);
	PUT8BIT(100, ptr);
	 
	ptr[0] = 2;
	ptr[1] = '.';
	ptr[2] = '.';
	ptr += 3;
	if (p->self->parent != NULL) {
		PUT32BIT(p->self->parent->inode, ptr);
	} else { // inode is root
		PUT32BIT(root_inode, ptr);
	}
	PUT8BIT(100, ptr);
	
	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		ptr[0] = p1->self->nleng - len_parent -1;
		ptr++;
		memcpy(ptr, &p1->self->name[len_parent+1], p1->self->nleng - len_parent - 1);
		ptr += (p1->self->nleng - len_parent -1); 
		PUT32BIT(p1->inode, ptr);
		PUT8BIT(p1->type, ptr);
	}

	return 0;
}
int8_t fs_getdir_from_drw(uint32_t inode, uint8_t *name_info) 
{
	fs_node *p = NULL;
	fs_node *p1 = NULL;
	uint32_t i = 0;
	uint8_t *ptr = NULL;

	if (inode == 1) {
		inode = root_inode;
	}

	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	} 
	
	ptr = name_info;
	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
		ptr[0] = p1->self->nleng;
		ptr++;
		memcpy(ptr, p1->self->name, p1->self->nleng);
		ptr += (p1->self->nleng); 
		PUT32BIT(p1->inode, ptr);
		PUT8BIT(p1->type, ptr);
		PUT64BIT(p1->self->size, ptr);
	}

	return 0;
}

void fs_statfs(uint64_t *totalspace,uint64_t *availspace,uint64_t *trspace,uint64_t *respace,uint32_t *inodes) {
	struct statfs disk_info;
	uint64_t avail_size = 0;
	uint64_t total_size = 0;
	int ret = 0;

	if ((ret = statfs(PATH, &disk_info)) == -1) {
		return;
	}

	total_size = disk_info.f_blocks * disk_info.f_bsize;
	avail_size = disk_info.f_bavail * disk_info.f_bsize;

	*totalspace = total_size;
	*availspace = avail_size;

	*inodes = 0;
	*trspace = 0;
	*respace = 0;
}

int8_t fs_read_chunk(uint32_t inode, uint32_t index, uint64_t *chunkid, uint64_t *file_size) 
{
	fs_node *p = NULL;

	if (inode == 1) {
		inode = root_inode;
	}
	
	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		return 3;
	}
	
	*file_size = p->self->size;
	if (*file_size == 0 || p->chunks == 0) {
		*chunkid = 0;
	} else {
		*chunkid = p->chunk_tab[index];
	}

	return 0;
}

int8_t add_chunkid_to_fs(uint32_t inode, uint32_t index, uint64_t *chunkid)
{
	fs_node *p = NULL;
	int8_t status;

	if (inode == 1) {
		fprintf(stderr, "inode is root inode, error!\n");
		return 1;
	}
	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		fprintf(stderr, "not find inode(%u)!\n", inode);
		return 3;
	}

	if (p->type == 100) {
		fprintf(stderr, "error, 目标为目录\n");
		return 2;
	}

	if (p->chunks < (index+1))  {// dat中chunkid不存在
		//dat create chunk file
fprintf(stderr, "-----------------index:%d\n", index);
		status = create_new_chunk(chunkid);
		if (status != 0) {
			fprintf(stderr, "create chunk failed\n");
			return 1;
		}

		if (p->chunks % 100 == 0) {
	  		p->chunk_tab = realloc(p->chunk_tab, (p->chunks+100)*sizeof(uint64_t));

	  		if (p->chunk_tab == NULL) {
		  		fprintf(stderr, "realloc chunk_tab failed\n");
		  		return 1;
	  		} 
		}
		p->chunk_tab[p->chunks] = *chunkid;
		p->chunks ++;
		change = 1; 

	} else {
		*chunkid = p->chunk_tab[0];
	}
	
	return 0;
}

int8_t change_file_size(uint32_t inode, uint32_t index, uint64_t size)
{
	fs_node *p = NULL;

	if (inode == 1) {
		fprintf(stderr, "inode is root inode, error!\n");
		return 1;
	}
	p = find_fsnode_by_inode(inode);
	if (p == NULL) {
		fprintf(stderr, "not find inode(%u)!\n", inode);
		return 3;
	}

	if (p->type == 100) {
		fprintf(stderr, "error, 目标为目录\n");
		return 2;
	}

	// 只有indx位于最后一个indx时，才改变大小，否则改变会导致文件莫名的缩小
	if (p->chunks == (index+1))  {// dat中chunkid不存在
		p->self->size = size;
		change = 1;
	} 

	return 0;
}

int8_t save_file_to_config(char *name, uint32_t inode, uint8_t type, uint64_t size)
{
	char buf[MAX_PATH_LEN] = {0};
	char data[MAX_NAME_LEN] = {0};
	int fd = 0;
	int ret;
			
	sprintf(buf, "%s/%u", CONFIG, root_inode);
	sprintf(data, "%s$%u$%u$%llu$0", name, inode, type,
				   (long long unsigned int)size);

	if (access(buf, F_OK) != 0) {
		fprintf(stderr, "parent(%u) dir lose\n", root_inode);
		return 1;
	}
	fd = open(buf, O_APPEND|O_WRONLY);
	if ((ret = write(fd, data, strlen(data))) != strlen(data)) {
		fprintf(stderr, "write error\n");
		return 1;
	}
	close(fd);

	return 0;
}

int8_t save_fs_config_to_local()
{
	char path[MAX_PATH_LEN] = {0};
	char buf[MAX_STR_LEN] = {0};
	uint8_t i = 0;
	uint8_t k = 0;
	FILE *fp = NULL;
	fs_node *p = NULL;
	fs_node *p1 = NULL;
			
	if (change == 0) {
		return 0;
	}

	p = find_fsnode_by_inode(root_inode);
	if (p == NULL) {
		return 0;
	}

	sprintf(path, "%s/%u", CONFIG, root_inode);

	fp = fopen(path, "w");
	if (fp == NULL) {
		fprintf(stderr, "open file(%s) failed\n", path);
		return 1;
	}

	// 首先保存父节点
	sprintf(buf, "%s$%u$%u$%llu$0\n", p->self->name, p->inode, p->type, 
					(long long unsigned int)p->self->size);
	fprintf(fp,buf);	

	if (p->self->n_child == 0) {
		return 0;
	}

	for (i = 0; i < p->self->n_child; i++) {
		p1 = p->self->child[i];
	
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%s$%u$%u$%llu$%u", p1->self->name, p1->inode, p1->type, 
					(long long unsigned int)p1->self->size, p1->chunks);

		for (k = 0; k < p1->chunks; k++) {
			sprintf(buf, "%s$%llu", buf, (long long unsigned int)p1->chunk_tab[k]);
		}
		sprintf(buf, "%s\n", buf);
		fprintf(fp,buf);	

// 目前仅仅考虑根节点
/*		if (p1->type == 100) {
			list_all_child(p1->inode);
		} */
	}	
	fclose(fp);
	
	change = 0;

	return 0;
}



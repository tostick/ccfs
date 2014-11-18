#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "sockets.h"
#include "data_pack.h"
#include "con_ser.h"
#include "r_data.h"
#include "w_data.h"
#include "../communication.h"


void translate_size(uint64_t size) 
{
	float t_size = 0.0;

	t_size = size;
	if (t_size < 1024) {
		printf("%4.0fB    ", t_size);
		return;
	}
	t_size /= 1024;
	if (t_size < 1024) {
		printf("%4.1fK    ", t_size);
		return;
	}
	t_size /= 1024;
	if (t_size < 1024) {
		printf("%4.1fM    ", t_size);
		return;
	}
	t_size /= 1024;
	if (t_size < 1024) {
		printf("%4.1fG    ", t_size);
		return;
	}
}

int8_t list_fs_info(uint8_t m_inode, uint8_t m_hide)
{
	uint8_t *buff;
	uint32_t len;	
	uint32_t i = 0;
	uint8_t nleng;
	char name[MAX_NAME_LEN] = {0};
	uint32_t inode;
	uint8_t type;
	uint64_t size;
	uint8_t *ptr = NULL;

	fs_info(1, 0, 0, &buff, &len);

	ptr = buff;
	while (i < len) {
		// get nleng
		GET8BIT(nleng, ptr);
		memset(name, 0, sizeof(name));
		memcpy(name, ptr, nleng);
		ptr += nleng;
		GET32BIT(inode, ptr);
		GET8BIT(type, ptr);
		GET64BIT(size, ptr);
		
		i += 14+nleng;

		if (!m_hide) {
			if (name[0] == '.') {
				continue;
			}
		}

		// output
		if (type == 100) {
			printf("d    ");
		} else if (type == 102) {
			printf("f    ");
		} else {
			printf("?    ");
		}

		if (m_inode) {
			printf("%10d    ", inode);
		}

		translate_size(size);
		printf("%s    \n", name);
	}

	return 0;
}

int8_t list_fs_volume()
{
        uint64_t totalspace,availspace,trashspace,reservedspace;
        uint32_t inodes;

        fs_statfs(&totalspace,&availspace,&trashspace,&reservedspace,&inodes);

	fprintf(stderr, "    ---------------------------    \n");

	fprintf(stderr, "total size:");
	translate_size(totalspace);
	printf("\n");

	fprintf(stderr, "free size:");
	translate_size(availspace);
	printf("\n");

	return 0;
}


void list_fs_usage(const char *name)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "%s -L\tlist all contents\n", name);
	fprintf(stderr, "parameter:\n");
	fprintf(stderr, "\t-i show inode info\n");
	fprintf(stderr, "\t-a show hide content\n\n");
}

void list_volume_usage(const char *name)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "%s -V\tlist fs volume\n\n", name);
}


void read_data_usage(const char *name)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "%s -R -s source -g goal\tRead data\n", name);
	fprintf(stderr, "parameter:\n");
	fprintf(stderr, "\t-s source path\n");
	fprintf(stderr, "\t-g dest path\n\n");
}


void write_data_usage(const char *name)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "%s -W -s source -g goal\tWrite data\n", name);
	fprintf(stderr, "parameter:\n");
	fprintf(stderr, "\t-s source path\n");
	fprintf(stderr, "\t-g dest path\n\n");
}


int list_fs(int argc, char **argv)
{
	int opt = 0;
	uint8_t m_list = 0;
	uint8_t m_inode = 0;
	uint8_t m_hide = 0;

	if (argc == 1) {
		list_fs_usage(argv[0]);
		return 1;

	}
	
	while ((opt = getopt(argc, argv, "iaLh")) != -1) {
		switch (opt) {
		case 'L':
			m_list = 1;
			break;
		case 'i':
			m_inode = 1;
			break;
		case 'a':
			m_hide = 1;
			break;
		case 'h':
		case '?':
			list_fs_usage(argv[0]);
			return 1;
		default:
			list_fs_usage(argv[0]);
			fprintf(stderr, "unknown parameter\n");
			return 1;
		}
	}
	
	if (m_list) {
		list_fs_info(m_inode, m_hide);
	}
	
	return 0;
}

int list_volume(int argc, char **argv)
{
	int opt = 0;
	uint8_t m_volume = 0;

	if (argc == 1) {
		list_volume_usage(argv[0]);
		return 1;

	}
	
	while ((opt = getopt(argc, argv, "Vh")) != -1) {
		switch (opt) {
		case 'V':
			m_volume = 1;
			break;
		case 'h':
		case '?':
			list_volume_usage(argv[0]);
			return 1;
		default:
			list_volume_usage(argv[0]);
			fprintf(stderr, "unknown parameter\n");
			return 1;
		}
	}
	
	if (m_volume) {
		list_fs_volume();
	}

	return 0;
}

int read_fs_data(int argc, char **argv)
{
	char *src = NULL;
	char *dest = NULL;
	int opt = 0;
	int ret = 1;
	uint8_t m_read = 0;

	if (argc == 1) {
		read_data_usage(argv[0]);
		return 1;

	}
	
	while ((opt = getopt(argc, argv, "s:g:Rh")) != -1) {
		switch (opt) {
		case 'R':
			m_read = 1;
			break;
		case 's':
			src = optarg;	
			break;
		case 'g':
			dest = optarg;	
			break;
		case 'h':
		case '?':
			read_data_usage(argv[0]);
			return 1;
		default:
			read_data_usage(argv[0]);
			fprintf(stderr, "unknown parameter\n");
			return 1;
		}
	}
	
	if (m_read) {
		if (src == NULL || dest == NULL) {
			fprintf(stderr, "file name is null!\n");
			return 1;
		}
		ret = read_data(src, dest);
	} else {
		read_data_usage(argv[0]);
		return 1;
	}

	if (ret == 0) {
		fprintf(stderr, "success\n");
	} else {
		fprintf(stderr, "failed\n");
	}

	return ret;
}


int write_fs_data(int argc, char **argv)
{
	char *src = NULL;
	char *dest = NULL;
	int opt = 0;
	int8_t ret = 1;
	uint8_t m_write = 0;

	if (argc == 1) {
		write_data_usage(argv[0]);
		return 1;

	}
	
	while ((opt = getopt(argc, argv, "s:g:Wh")) != -1) {
		switch (opt) {
		case 'W':
			m_write = 1;
			break;
		case 's':
			src = optarg;	
			break;
		case 'g':
			dest = optarg;	
			break;
		case 'h':
		case '?':
			write_data_usage(argv[0]);
			return 1;
		default:
			write_data_usage(argv[0]);
			fprintf(stderr, "unknown parameter\n");
			return 1;
		}
	}
	
	if (m_write) {
		if (src == NULL || dest == NULL) {
			fprintf(stderr, "file name is null!\n");
			return 1;
		}
		// 判断src 存在性以及是否可读
		if (access(src, R_OK) != 0) {
			fprintf(stderr, "file(%s) is unexist or not be read!\n", src);
			return 1;
		}

		ret = write_data(src, dest);
	} else {
		write_data_usage(argv[0]);
		return 1;
	}

	if (ret == 0) {
		fprintf(stderr, "success\n");
	} else {
		fprintf(stderr, "failed\n");
	}

	return ret;
}


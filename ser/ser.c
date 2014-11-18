#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "ser.h"
#include "init.h"
#include "fs.h"
#include "con_cli.h"
#include "con_dat.h"


static int32_t now;

static pthread_t fsthid;
static pthread_t datthid;
static pthread_t drwthid;


typedef struct selentry {
	int (*desc)(fd_set *, fd_set *);
	void (*serv)(fd_set *, fd_set *);
	struct selentry *next;
}selentry;

static selentry *selhead = NULL;

void register_cli(int (*desc)(fd_set *, fd_set *), void (*serv)(fd_set *, fd_set *))
{
	selentry *aux = (selentry *)malloc(sizeof(selentry));
	aux->desc = desc;
	aux->serv = serv;
	aux->next = selhead;
	selhead = aux;
}

void *fs_thread(void *arg)
{
	while (1) {
		// 以后加锁保存，防止数据在读写操作
		save_fs_config_to_local();
		sleep(10);
	}
}

void *drw_thread(void *arg)
{
	connect_cli();
	return NULL;
}

void *dat_thread(void *arg)
{
	connect_dat();
	return NULL;
}

int initser()
{
	now = time(NULL);

	load_fs();

	// 创建线程保存配置信息(当发生变化时，10s保存一次)
	pthread_create(&fsthid, NULL, fs_thread, NULL);
	// 创建线程监听dat and drw
	pthread_create(&drwthid, NULL, drw_thread, NULL);
	pthread_create(&datthid, NULL, dat_thread, NULL);

	return 0;
}

void loopser()
{
	struct timeval tv;
	selentry *selit = NULL;
	fd_set rset;
	fd_set wset;
	int max;
	int i;

	while(1) {
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		max = -1;

		for(selit = selhead; selit; selit=selit->next) {
			i = selit->desc(&rset, &wset);
			if (i > max) {
				max = i;
			}
		}

		i = select(max+1, &rset, &wset, NULL, &tv);
		gettimeofday(&tv, NULL);
		now = tv.tv_sec;

		if (i < 0) {
			fprintf(stderr, "select err\n");
		} else {
			for(selit = selhead; selit; selit=selit->next) {
				selit->serv(&rset, &wset);
			}
		}
	
	}

}

uint32_t main_time()
{
	return now;
}

int main(int argc, char **argv)
{
	if (!initser()) {
		loopser();
	}
	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "dat.h"
#include "con_ser.h"
#include "con_cli.h"
#include "mgr_hd.h"

static int32_t now;


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
	char *host = "127.0.0.1";
	char *port = "2016";
	// connect to master
	hd_init();
	ser_init(host, port);

	connect_cli();	
	loopser();

	return 0;
}

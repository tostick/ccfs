#ifndef _INIT_H_
#define _INIT_H_

#include "con_cli.h"
#include "con_dat.h"
#include "chunks.h"

typedef int (*module_func) (void);

struct {
	module_func func;
	char *msg;
}module_groups[] = {
//	{ser_init, "ser init"},
//	{connect_cli, "connect client"},
//	{connect_dat, "connect data client"},
	{NULL, NULL}
};

#endif

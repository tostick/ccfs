#ifndef _CSER_H_
#define _CSER_H_

#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>

void register_cli (int (*desc)(fd_set *,fd_set *),void (*serv)(fd_set *, fd_set *));
uint32_t main_time();

#endif


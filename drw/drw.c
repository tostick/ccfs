#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "con_ser.h"
#include "fs_operation.h"

#define	drw_history	"/tmp/.drw"
#define	MAX_CMD_ARG_NUM 64


pthread_mutex_t main_lock;

typedef	int (*_func)(int argc,char *argv[]);
struct command
{
        char   *name;
        _func   func;
        char   *cmd_msg;
};

int quit(int argc,char *argv[]);
int help(int argc,char *argv[]);
int version(int argc,char *argv[]);

struct command command_table[] = {
	 {"list",list_fs,"list fs data"},
	 {"show_volume",list_volume,"list fs volume"},
	 {"read",read_fs_data,"read data from fs"},
	 {"write",write_fs_data,"write data to fs"},
	 {"version",version,"Show verion info"},
	 {"quit",quit,"Quit"},
	 {"help",help,"Help info"},
	 {NULL},
	};

int func_log_begin (int argc, char **argv)
{
        int     i = 0;
        char    msg[1024] = { 0 };

        strcpy (msg, "FUNCTION BEGIN(");
        while (i < argc)
        {
                strncat (msg, argv[i++], sizeof (msg));
                if (i < argc)
                        strncat (msg, " ", sizeof (msg));
        }
        strncat (msg, ")", sizeof (msg));
        return 0;
}

int func_log_end (const char *msg)
{
        return 0;
}

int quit(int argc,char *argv[])
{
	_exit(0);
}

int help(int argc,char *argv[])
{
	struct	command *p_cmd;

	p_cmd = command_table;
	printf ("  Available kvm commands:\n");
 
       	while (p_cmd->name)
       	{
                printf ("  %-35s  %s\n", p_cmd->name, p_cmd->cmd_msg);
               	p_cmd++;
       	}
 
         return 0;
}


int version(int argc,char *argv[])
{
	if(argc>=2) {
		return	-1;
	}

	printf("drw 1.0,build %s %s\n",__DATE__,__TIME__);
	printf("Copyright (C) 2014 United Information Technology Co.,Ltd\n");
	return	0;
}

static char *_list_cmds (const char *text, int state)
{
        static size_t len = 0;
        static struct command *cmd_p;
        char   *ret_line = NULL;

        static int cmd_tbl_offset = 0;

        if (!state)
        {
                len = strlen (text);
                cmd_tbl_offset = 0;
                cmd_p = command_table;
        }

        while (!ret_line)
        {
                if (!cmd_p || !cmd_p->name)
                {
                        cmd_tbl_offset++;
                        if (cmd_tbl_offset >= (sizeof (command_table) / sizeof (struct command)))
                                break;
                        cmd_p = command_table + cmd_tbl_offset;
                        if (!cmd_p)
                                break;
                }

                while (cmd_p->name && !ret_line)
                {

                        /* Found the register cmd, execute it and exit */
                        if (!strncmp (text, cmd_p->name, len))
                        {
                        	ret_line = strdup (cmd_p->name);
                        }
                        cmd_p++;
                }
        }

        return ret_line;
}

int run_command (int argc, char *argv[])
{
        int     ret = -1;
        char   *cmd;
        struct command *p_cmd;

        cmd = argv[0];
        for (p_cmd = command_table; p_cmd->name; p_cmd++)
        {
                if (!strcmp (cmd, p_cmd->name))
                {
                        ret = p_cmd->func (argc, argv);
                        return ret;
                }
        }

        fprintf (stdout, "%s not found. \n", cmd);

        return ret;
}


static char **_completion (const char *text, int start_pos, int end_pos __attribute ((unused)))
{
        char  **match_list = NULL;
        int     p = 0;


        while (isspace ((int) *(rl_line_buffer + p)))
                p++;

        if (start_pos == p)
        {
                match_list = rl_completion_matches (text, _list_cmds);
        }
        else
        {
                int     end_p = p;
                while (!isspace ((int) *(rl_line_buffer + end_p)))
                        end_p++;

                char   *topic = strndupa (rl_line_buffer + p, end_p - p);

                printf ("\n");
                run_command (2, (char *[])
                                         {
                                         "help", (char *) topic, NULL}
                );

                rl_reset_line_state ();
        }


        rl_attempted_completion_over = 1;
        return match_list;
}



int main(int argc, char **argv)
{
	char   *line;
	int     i, ret = 0;
	char   *delim = " \t", *token = NULL;
	char   *prompt = "drw> ";
	char   *cmd_args[MAX_CMD_ARG_NUM];
	
	char *host = NULL;
	char *port =NULL;

	if (host == NULL) {
		host = strdup("127.0.0.1");
	}
	if (port == NULL) {
		port = strdup("2014");
	}

	
	pthread_mutex_init(&main_lock,NULL);

	pthread_mutex_lock(&main_lock);
	ser_init(host, port);

	if (argc == 1)
	{
		sleep(1);

		signal (SIGINT, SIG_IGN);
		signal (SIGQUIT, SIG_IGN);
		signal (SIGPIPE, SIG_IGN);
		signal (SIGTSTP, SIG_IGN);


		rl_readline_name = "drw";
		rl_attempted_completion_function = _completion;

		stifle_history(500);
		read_history(drw_history);
		while (1)
		{
			line = readline (prompt);

			if (!line)
			{
				printf ("\n");
				break;
			}

			if (strlen (line))
			{
				write_history(drw_history);
				add_history (line);
				
				i = 0;
				memset (cmd_args, 0, sizeof (cmd_args));
				token = strtok (line, delim);
				while (token != NULL)
				{
					cmd_args[i] = token;
					i++;
					token = strtok (NULL, delim);
				}

				cmd_args[i] = NULL;
				optind = 0;
				if (i > 0)
					run_command (i, cmd_args);
			}
			free (line);
		}
	}
	else
	{
		argc--;
		argv++;
		ret = run_command (argc, argv);
	}
	pthread_mutex_unlock(&main_lock);
	close_service(); 

	return ret;
}

/******************************************************************************
* Copyright (c) 2019, Fehmi Noyan ISI fnoyanisi@yahoo.com
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY Fehmi Noyan ISI ''AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Fehmi Noyan ISI BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Standard header files */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* socket related stuff */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* MySQL API */
#include <mysql.h>

/* socket related defines */
#define PORTNO	21210	
#define QLEN	10
#define BUFLEN	256

#include "dbcred.h"
#include "restfuld.h"

#define NAMVALLEN 2

#define LOCKFILE "/var/run/restfuld.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define LOGPATH "/var/log/" 

struct thread_data {
	int fd;
	struct mysql_db_cred dbcred;
};

/*
 * Executes an SQL statement and sends each result row to the client
 * in JSON format. 
 */
void
execute_sql(char *q, int sock, const struct mysql_db_cred *cred,  
    ssize_t (*p_func) (int, const void *, size_t, int))
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	MYSQL *conn, mysql;
	int state, numrows;
	char sql[256], buf[256];

	if (mysql_init(&mysql) == NULL)
		err(1, "Insufficient memory to initialize MySQL connection");

	if ((conn = mysql_real_connect(&mysql, cred->hostname, 
					       cred->username, 
					       cred->password, 
					       cred->database,  
	    0, 0, 0)) == NULL)
		err(1, "%s", mysql_error(&mysql));

	sprintf(sql, "select * from test where name like '%%%s%%'", q);

	if ((state = mysql_query(conn, sql)) != 0)
		err(1, "%s", mysql_error(&mysql));

	/*
	 * Place the result of the query into a MYSQL_RES struct 
	 */
	result = mysql_store_result(conn);
	
	numrows = mysql_num_rows(result);

	while ((row = mysql_fetch_row(result)) != NULL) {
		sprintf(buf, "{\n\t\"person\": { 		\
				\n\t\t\"Name\": \"%s\", 	\
				\n\t\t\"Surname\": \"%s\", 	\
				\n\t\t\"Age\": \"%s\" 		\
				\n\t} 				\
				\n}\n", 			\
		    (row[0] ? row[0] : "NULL"),
		    (row[1] ? row[1] : "NULL"),
		    (row[2] ? row[2] : "0"));
		p_func(sock, buf, strlen(buf), 0);
	}

	/* Done with the result set, free the allocated memory */
	mysql_free_result(result);
	mysql_close(conn);
}

/*
 * Extracts the name-value pairs from a HTTP GET request
 * sent by the client. Each name field is assumed to be single char.
 * We only are interested in the GET .... part of the header,
 * so anything else is ignored.
 * At most len number of name-value pairs are extracted.
 * In case of an error, -1 is returned. When the buffer pointed
 * by buf is not a line containing a GET request, 1 is returned.
 * On success, 0 is returned.  
 */
int
get_http_values(const char *buf, struct http_get_req *r, int len)
{
        char namval[2048], *s, *e, *p;

        if (memcmp(buf, "GET ", 4) != 0)
               return (1);

        /* start & end of name-value pairs */
        if ((s = strchr(buf, '?')) == NULL)
                return (-1);
        s++;
        if ((e = strchr(s, ' ')) == NULL)
                return (-1);

        strlcpy(namval, s, (e - s + 1));
        p = namval;

        while (((p = strtok(p, "&")) != NULL) && (len-- > 0)){
                r->name = *p;
                strlcpy(r->value, strchr(p, '=')+1, VALBUFLEN);
                r++;
                p = NULL;
        }

        return 0;
}

/* 
 * Sends HTTP related headers to the client
 */
void
send_http_headers(int client)
{
	int buflen = 1024;
	char buf[buflen];

	strlcpy(buf, "HTTP/1.0 200 OK\r\n", buflen);
	send(client, buf, strlen(buf), 0);
	strlcpy(buf, "Server: Httpd/0.1.0\r\n", buflen);
	send(client, buf, strlen(buf), 0);
	strlcpy(buf, "Content-Type: application/json\r\n", buflen);
	send(client, buf, strlen(buf), 0);
	strlcpy(buf, "\r\n", buflen);
	send(client, buf, strlen(buf), 0);
}

/*
 * Prints the usage and exits
 */
void
usage(void)
{
	fprintf(stderr ,"usage: restfuld [-D database] [-H host] " 
			"[-l logfile] [-p portno] [-P password] "
			"[-T table] [-U user]\n"); 
	exit(1);
}

void *
worker(void *p)
{
	char buf[BUFLEN];
	int i, fd, recv_bytes;
	struct http_get_req namval[NAMVALLEN];
	struct mysql_db_cred cred;
	struct thread_data *d;

	d = (struct thread_data *)p;
	fd = d->fd;
	cred = d->dbcred;

	/* clear the receive buffer */
	memset(buf, 0, BUFLEN);

	/* read the client request */
	if ((recv_bytes = read(fd, buf, BUFLEN - 1)) < 0) {
		syslog(LOG_ERR, "read: %m");
		return NULL;
	}

	/* send data to the client */
	send_http_headers(fd);
	if(get_http_values(buf, namval, NAMVALLEN) == 0){
		for (i=0; i < NAMVALLEN; i++) {
			if (namval[i].name == 'q') {
				execute_sql(namval[i].value, 
				    fd, &cred, send);
			}
		}
	}

	if (shutdown(fd, SHUT_RDWR) < 0)
		err(1, (char *)NULL);

	if (close(fd) < 0)
		err(1, (char *)NULL);

	return NULL;
}

/*
 * Simple daemon that implements a RESTful API
 * for MySQL server.
 */
int
main(int argc, char **argv)
{
	pid_t pid;
	pthread_t t_id;
	int fd, sock, newsock, port;
	int ch, cflag, Dflag, Hflag, lflag, pflag, Pflag, Tflag, Uflag;
	char logpath[PATH_MAX], cfgpath[PATH_MAX], pidbuf[32];
	socklen_t addr_len;
	struct sockaddr_in srv_addr, cli_addr;
	struct thread_data d;
	struct mysql_db_cred cred;
	struct sigaction sa;

	/* set the defaults */
	port = PORTNO;

	(void)strlcpy(cred.hostname, DBHOST, DBHOST_LEN);
	(void)strlcpy(cred.username, DBUSER, DBUSER_LEN);
	(void)strlcpy(cred.password, DBPASS, DBPASS_LEN);
	(void)strlcpy(cred.username, DBNAME, DBNAME_LEN);
	(void)strlcpy(cred.table, DBTABLE, DBTABLE_LEN);

	(void)strlcpy(logpath, LOGPATH"restfuld.log", PATH_MAX);
	(void)strlcpy(cfgpath, "./restfuld.conf", PATH_MAX);

	/* command line arguments */
	cflag = Dflag = Hflag = lflag = pflag = Pflag = Tflag = Uflag = 0;
	while ((ch = getopt(argc, argv, "c:D:H:l:p:P:T:U:")) != -1) {
		switch(ch) {
			case 'c':
				cflag = 1;
				if (access(dirname(optarg), W_OK) != 0)
					err(1, (char *)NULL);
				strlcpy(cfgpath, optarg, PATH_MAX);
			case 'D':
				Dflag = 1;
				strlcpy(cred.database, optarg, DBNAME_LEN);
				break;
			case 'H':
				Hflag = 1;
				strlcpy(cred.hostname, optarg, DBHOST_LEN);
				break;
			case 'l':
				lflag = 1;
				if (access(dirname(optarg), W_OK) != 0)
					err(1, (char *)NULL);
				strlcpy(logpath, optarg, PATH_MAX);
				break;
			case 'p':
				pflag = 1;
				if ((port = (int)strtol(optarg, NULL, 10)) 
				    == 0){
					fprintf(stderr, "restfuld: "
						"Invalid port number\n");
					exit(1);
				}
				break;
			case 'P':
				Pflag = 1;
				strlcpy(cred.password, optarg, DBPASS_LEN);
				break;
			case 'T':
				Tflag = 1;
				strlcpy(cred.table, optarg, DBTABLE_LEN);
				break;
			case 'U':
				Uflag = 1;
				strlcpy(cred.username, optarg, DBUSER_LEN);
				break;
			case '?':
			default:
				usage();	

		}
	}
	argc -= optind;
	argv += optind;

	/* daemonize */
	
	/* clear the file creation mask */
	umask(0);

	pid = fork();
	if (pid < 0) {
		/* error */
		fprintf(stderr,"restfuld: Cannot create background process.\n");
		exit(1);
	}

	/* parent */
	if (pid > 0) {
		printf("restfuld started with PID %d.\n", pid);	
		exit(0);
	}

	/* child */
	if (setsid() < 0) {
		perror("setsid");
		exit(1);
	}

	/* Ignore SIGHUP and SIGTERM */
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
#ifdef SA_INTERRUPT
	sa.sa_flags |= SA_INTERRUPT;
#endif
	if (sigaction(SIGHUP, &sa, NULL) < 0) {
		perror("sigaction SIGHUP");
		exit(1);
	}

	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		perror("sigaction SIGTERM");
		exit(1);
	}

	/* change the working directory for the daemon */
	if (chdir("/") < 0) {
		perror("chdir");
		exit(1);
	}

	/* 
	 * close all open file descriptors and connect stdin, 
	 * stdout and stderr to /dev/null
	 */
	for (fd = 0; fd < getdtablesize(); fd++)
		(void)close(fd);

	if ((fd = open("/dev/null", O_RDWR)) < 0) {
		perror("open /dev/null");
		exit(1);
	}

	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);

	/* Initialize the log file */
	openlog("restfuld", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
	syslog(LOG_INFO, "restfuld started.\n");

	/* Done with daemonizing */

	/* Make sure only one instance of restfuld is running */
	if ((fd = open(LOCKFILE, O_RDWR | O_CREAT, LOCKMODE)) < 0) {
		syslog(LOG_ERR,"cannot open %s: %s", LOCKFILE, strerror(errno));
		exit(1);
	}

	if (lockf(fd, F_TLOCK, 0) < 0) {
		if (errno == EACCES || errno == EAGAIN) {
			syslog(LOG_ERR,"another instance is already running.");
			close(fd);
			exit(1);
		}
		syslog(LOG_ERR, "flock: %m");
		exit(1);
	}

	ftruncate(fd, 0);
	memset(pidbuf, 0, sizeof(pidbuf)); 
	(void)snprintf(pidbuf,sizeof(pidbuf), "%ld",(long)getpid());
	if (write(fd, pidbuf, strlen(pidbuf)+1) < 0) {
		syslog(LOG_ERR,"cannot write %s: %s", 
		    LOCKFILE, strerror(errno));
		exit(1);
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syslog(LOG_ERR, "socket: %m");
		exit(1);
	}

	memset(&srv_addr, 0, sizeof(srv_addr));
	memset(&cli_addr, 0, sizeof(cli_addr));

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = INADDR_ANY;
	srv_addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		syslog(LOG_ERR, "bind: %m");
		exit(1);
	}

	if (listen(sock, QLEN) < 0) {
		syslog(LOG_ERR, "listen: %m");
		exit(1);
	}

	for(;;) {
		addr_len = sizeof(cli_addr);

		/* accept incoming connections - this call is blocking */
		if ((newsock = accept(sock, (struct sockaddr *)&cli_addr, 
		    &addr_len)) < 0) {
			syslog(LOG_ERR, "accept: %m");
			exit(1);
		}

		d.fd = newsock;
		d.dbcred = cred;

		/*
		 * We have a connection from a client at this point.
		 * Create a new thread to handle the request.
		 */
		if (pthread_create(&t_id, NULL, worker, (void *)&d) < 0) { 
			syslog(LOG_ERR, "could not create the new thread!");
			exit(1);
		}
	}

	if (close(sock) < 0) {
		syslog(LOG_ERR,"close: %m");
		closelog();
		exit(1);
	}
	
	closelog();
	
	return 0;
}

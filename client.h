#ifndef _CLIENT_H_
#define _CLIENT_H_


#include <unistd.h>     // close read write
#include <string.h>     // memset
#include <errno.h>      // perror
#include <stdlib.h>     // exit
#include <sys/stat.h>   // stat
#include <dirent.h>     // readdir DIR
#include <netdb.h>      // sockaddr_in
#include <sys/wait.h>   // wait


#include "hash.h"       // hash()
#include "ftree.h"      // request stuct

/**
 * Initialize a client socket
 * @param  host the host address
 * @return      the socket file descriptor
 */
int client_sock(char *host, unsigned short port);

int main_client_wait();

/**
 * traverse the file rooted at src
 * @param  sock_fd the socket file descriptor
 * @return         0 on success; -1 on failure.
 */
int traverse(int sock_fd, char *src_path, char *server_path, char *host,
	unsigned short port);

#endif

#ifndef _SERVER_H_
#define _SERVER_H_

#include <unistd.h>     // close read write
#include <string.h>     // memset
#include <errno.h>      // perror
#include <stdlib.h>     // exit
#include <sys/stat.h>   // stat
#include <netdb.h>      // sockaddr_in

#include "hash.h"       // hash()
#include "ftree.h"      // request stuct

// for read request
#define WAIT_TYPE 0
#define WAIT_PATH 1
#define WAIT_MODE 2
#define WAIT_HASH 3
#define WAIT_SIZE 4
#define WAIT_DATA 5
#define WAIT_OK 6

// for handle client flag
#define HANDLE_OK 0				// handle was successful
#define HANDLE_READOK 1			// the read was ok but not finished
#define HANDLE_READDONE 2		// the read was ok and there are no more field
#define HANDLE_DONE 3			// the handling was done entirely


/**
 * A client Link List node
 * fd				file descriptor of the file
 * current_state	the current state of the client
 * file				the file to be synced
 * client_req		the client request
 * next				the next client node
 */
struct client {
    int fd;
    int current_state;
    FILE *file;
    struct request client_req;
	struct in_addr ipaddr;
    struct client *next;
};

/**
 * Initialize a server socket descriptor and set, bind and listen
 * @return the listening file descriptor for server
 */
int server_sock();

/**
 * handle the client at cp
 * @param  cp   the pointer pointing to the client
 * @param  head the first client in the link list
 * @return      0 on success, -1 otherwise.
 */
int handle_client(struct client *cp, struct client *head);

/**
 * Add a client to the head of the client link list
 * @param  head      the current head of the client link list
 * @param  client_fd the client file descriptor to add
 * @param  sin_addr  the in_addr of the new client
 * @return           the new head of the client link list
 */
struct client *add_client(struct client *head, int client_fd,
						  struct in_addr sin_addr);

/**
 * Remove a client from the client LL
 * @param  top the pointer to the first client in the LL
 * @param  fd  the file descriptor of the client to be deleted
 * @return     the new pointer to the first client in the LL
 */
struct client *remove_client(struct client *head, int fd);

/**
 * Helper function that reads the request sent by the client.
 * @param  cp the client pointer
 * @return    HANDLE_READOK if current read is successful and waiting for
 *                          another field
 *            ERROR if error encountered
 *            HANDLE_DONE if the socket is closed
 *            HANDLE_READDONE if the all fields have been read
 */
int read_request(struct client *cp);

#endif // _SERVER_H_

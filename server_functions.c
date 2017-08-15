#include <arpa/inet.h>
#include <stdio.h>

#include "ftree.h"
#include "hash.h"
#include "server.h"

static int make_dir(struct client *cp);
static int compare(struct request *request);
static int read_data(struct client *cp);

/**
 * Initialize a server socket descriptor and set, bind and listen
 * @return the listening file descriptor for server
 */
int server_sock() {
	int listen_fd;
	int on = 1;
	struct sockaddr_in server;

	server.sin_family = PF_INET;		 // allow sockets across machines
	server.sin_port = htons(PORT);		 // which port will we be listening on
	server.sin_addr.s_addr = INADDR_ANY; // listen on all network addresses
	bzero(&(server.sin_zero), 8);

	// set up listening socket soc
	if ((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("server_sock: socket");
		return -1;
	}
	// Make sure we can reuse the port immediately after the
	// server terminates. Avoids the "address in use" error
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on,
				   sizeof(on)) < 0) {
		perror("server_sock: setsockopt");
	}
	// Associate the process with the address and a port
	if (bind(listen_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("server_sock: bind");
		close(listen_fd);
		return -1;
	}
	// Sets up a queue in the kernel to hold pending connections
	if (listen(listen_fd, MAXCONNECTION) < 0) {
		perror("server_sock: listen");
		close(listen_fd);
		return -1;
	}

	return listen_fd;
}


/**
 * Add a client to the head of the client link list
 * @param  head      the current head of the client link list
 * @param  client_fd the client file descriptor to add
 * @param  sin_addr  the in_addr of the new client
 * @return           the new head of the client link list
 */
struct client *add_client(struct client *head, int client_fd,
						  struct in_addr sin_addr) {
	struct client *p = malloc(sizeof(struct client));
	if (!p) {
		perror("malloc");
		return NULL;
	}

	// initialize a meaningless request
	struct request client_request = {-1, "\0", -1, "\0", -1};

	p->fd = client_fd;
	p->current_state = WAIT_TYPE;
	p->file = NULL;
	p->client_req = client_request;
	p->ipaddr = sin_addr;
	p->next = head;
	head = p;
	return head;
}


/**
 * Remove a client from the client LL
 * @param  top the pointer to the first client in the LL
 * @param  fd  the file descriptor of the client to be deleted
 * @return     the new pointer to the first client in the LL
 */
struct client *remove_client(struct client *head, int client_fd) {
	struct client **p;
	// traverse the LL
	for (p = &head; *p && (*p)->fd != client_fd; p = &(*p)->next)
		;
	// Now, p points to (1) top, or (2) a pointer to another client
	// This avoids a special case for removing the head of the list
	if (*p) {
		struct client *t = (*p)->next;
		free(*p);
		*p = t;
	} else {
		fprintf(stderr,
				"remove_client: Trying to remove client_fd %d, but I don't "
				"know about it\n",
				client_fd);
	}
	return head;
}


/**
 * Handle the client at cp
 * @param  cp   the pointer pointing to the client
 * @param  head the first client in the link list
 * @return      HANDLE_OK		if handle is successful
 *              -1			if error occured
 *              HANDLE_DONE	if socket is closed
 *              HANDLE_READOK	if need to read more fields
 */
int handle_client(struct client *cp, struct client *head) {
	int result = read_request(cp);
	if (result != HANDLE_READDONE) {
		return result;
	}

	// if all fields are read then compare the file/dir and sync
	struct request *request = &(cp->client_req);
	printf("path: %s; type: %d; mode: %u; hash: %s; size: %u\n", request->path,
		   request->type, request->mode, request->hash, request->size);

	if (request->type == REGFILE || request->type == REGDIR) { // Main client
		// compare file and send new request;
		result = compare(request);
		if (result < 0) {
			fprintf(stderr, "handle_client: compare\n");
			return -1;
		}
		result = htonl(result);
		if (write(cp->fd, &result, sizeof(int)) < 0) {
			perror("handle_client: write");
			return -1;
		}

		cp->current_state = WAIT_TYPE;

	} else if (request->type == TRANSFILE) { // File transfer client
		result = -1;

		if (S_ISDIR(request->mode)) { // dir
			result = make_dir(cp);
			if (result < 0) {
				fprintf(stderr, "handle_client: make_dir: %s\n",
						cp->client_req.path);
				return -1;
			}

		} else if (S_ISREG(request->mode)) { // file
			if (!cp->file) { // if file does not exist, create file
				if (!(cp->file = fopen(request->path, "wb"))) {
					perror("fopen");
					return -1;
				}
			}
			if (request->size > 0) { // if file has content then write file
				result = read_data(cp);
				if (result < 0) {
					fprintf(stderr, "handle_client: read_data: %s\n",
							cp->client_req.path);
					return -1;
				}
			} else { // if file does not have content
				request = OK;
				if (write(cp->fd, &request, sizeof(int)) < 0) {
					perror("make_dir: write");
					return -1;
				}
				return result;
			}

		} else { // Unsupported file type
			fprintf(stderr, "Unsupported file type\n");
			return -1;
		}
		return result;

	} else {
		fprintf(stderr, "handle_client: unknown request type: %s\n",
				request->path);
		return ERROR;
	}

	return HANDLE_OK;
}


/**
 * Helper function that reads the request sent by the client.
 * @param  cp the client pointer
 * @return    HANDLE_READOK if current read is successful and waiting for
 *                          another field
 *            ERROR if error encountered
 *            HANDLE_DONE if the socket is closed
 *            HANDLE_READDONE if the all fields have been read
 */
int read_request(struct client *cp) {
	ssize_t len;
	struct request *request = &cp->client_req;

	switch (cp->current_state) {
	case WAIT_TYPE: {
		if ((len = read(cp->fd, &request->type, sizeof(int))) < 0) {
			perror("read_request: read type");
			return ERROR;
		} else if (len == 0) { // socket closed
			return HANDLE_READDONE;
		}
		request->type = ntohl(request->type);
		cp->current_state = WAIT_PATH;
		break;
	}
	case WAIT_PATH: {
		if ((len = read(cp->fd, request->path, MAXPATH)) < 0) {
			perror("read_request: read path");
			return ERROR;
		} else if (len == 0) {
			fprintf(stderr, "read_request: socket closed when reading path. "
							"Closing socket\n");
			return -1;
		}
		cp->current_state = WAIT_MODE;
		break;
	}
	case WAIT_MODE: {
		if ((len = read(cp->fd, &request->mode, sizeof(mode_t))) < 0) {
			perror("read_request: read mode");
			return ERROR;
		} else if (len == 0) {
			fprintf(stderr, "read_request: socket closed when reading mode. "
							"Closing socket\n");
			return -1;
		}
		request->mode = ntohs(request->mode);
		cp->current_state = WAIT_HASH;
		break;
	}
	case WAIT_HASH: {
		if ((len = read(cp->fd, request->hash, BLOCKSIZE)) < 0) {
			perror("read_request: read hash");
			return ERROR;
		} else if (len == 0) {
			fprintf(stderr, "read_request: socket closed when reading hash. "
							"Closing socket\n");
			return -1;
		}
		cp->current_state = WAIT_SIZE;
		break;
	}
	case WAIT_SIZE: {
		if ((len = read(cp->fd, &request->size, sizeof(size_t))) < 0) {
			perror("read_request: read size");
			return ERROR;
		} else if (len == 0) {
			fprintf(stderr, "read_request: socket closed when reading size. "
							"Closing socket\n");
			return -1;
		}
		request->size = ntohs(request->size);
		cp->current_state = WAIT_OK;
		return HANDLE_READDONE;
	}
	case WAIT_OK: {
		return HANDLE_READDONE;
	}
	}

	return HANDLE_READOK;
}


/**
 * Helper function that compares the server file with the original file.
 * @param  request the client request
 * @return         SENDFILE 		if the server does not have the file or the
 *                          		server's file is different from the original
 *                            		file.
 *                 OK				if the server has exactly the same file.
 *                 ERROR			if the server has different file type.
 *                 -1		if error occured during compare.
 */
static int compare(struct request *request) {
	struct stat server_stat;

	// get stat and check if file exist
	if (lstat(request->path, &server_stat) < 0) {
		if (errno != ENOENT) {
			perror("compare: lstat");
			return -1;
		} else {
			return SENDFILE;
		}
	}

	if (request->type == REGFILE) {

		if (!S_ISREG(server_stat.st_mode)) { // check if both are REGFILE
			fprintf(stderr, "compare: the files are not compatible: %s\n",
					request->path);
			return ERROR;
		}
		// compare size and hash
		FILE *f;
		if (!(f = fopen(request->path, "rb"))) {
			perror("compare: fopen");
			return -1;
		}
		char server_hash[BLOCKSIZE] = "\0";
		hash(server_hash, f);
		if (check_hash(server_hash, request->hash) == 0 &&
			server_stat.st_size == request->size) {
			return OK;
		} else {
			return SENDFILE;
		}

	} else {
		// make dir
		if (!S_ISDIR(server_stat.st_mode)) {
			fprintf(stderr, "compare: the files are not compatible: %s\n",
					request->path);
			return ERROR;
		}

		return OK;
	}
	return OK;
}

/**
 * Helper function that makes a directory and send the response to the client
 * @param  cp the client pointer
 * @return    0 on success; -1 on failure
 */
static int make_dir(struct client *cp) {
	struct request *req = &(cp->client_req);
	if (mkdir(req->path, req->mode) < 0) {
		perror("make_dir: mkdir");
		return -1;
	}

	int response = htonl(OK);
	if (write(cp->fd, &response, sizeof(int)) < 0) {
		perror("make_dir: write");
		return -1;
	}

	return HANDLE_DONE;
}

/**
 * read the data one MAXDATA bytes a time and write the data into the file
 * @param  cp the client pointer
 * @return    HANDLE_OK			if the current file is not entirely copied
 *            HANDLE_DONE		if the current file is done copying
 *            -1				if error occurred
 */
static int read_data(struct client *cp) {
	char buf[MAXDATA];
	int num_read, num_wrote;
	if ((num_read = read(cp->fd, buf, MAXDATA)) < 0) {
		perror("read_data: read");
		return -1;
	}

	if ((num_wrote = fwrite(buf, 1, num_read, cp->file)) != num_read) {
		if (ferror(cp->file)) {
			fprintf(stderr, "server:fwrite error for [%s]\n",
					cp->client_req.path);
			return -1;
		}
	}

	// if all bytes are read then send a response to the client
	if (num_wrote < MAXDATA) {
		if (fclose(cp->file) < 0) {
			perror("read_data: fclose");
			return -1;
		}
		int response = htonl(OK);
		if (write(cp->fd, &response, sizeof(int)) < 0) {
			perror("read_data: write");
			return -1;
		}
		return HANDLE_DONE;
	}

	return HANDLE_OK;
}

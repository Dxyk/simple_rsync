#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "client.h"
#include "ftree.h"
#include "hash.h"

static int generate_request(int sock_fd, char *src_path, char *server_path,
							struct request *request);
static int send_request(int sock_fd, struct request *request);
static int send_data(int sock_fd, char *src_path);

int CHILD_COUNT = 0;


/**
 * Initialize a client socket.
 * @param  host the host address.
 * @return      the socket file descriptor.
 */
int client_sock(char *host, unsigned short port) {
	int sock_fd;
	struct hostent *hp;
	struct sockaddr_in peer;

	peer.sin_family = PF_INET;
	peer.sin_port = htons(port);

	/* fill in peer address */
	hp = gethostbyname(host);
	if (hp == NULL) {
		fprintf(stderr, "client_sock: %s unknown host\n", host);
		return -1;
	}

	peer.sin_addr = *((struct in_addr *)hp->h_addr);

	/* create socket */
	if ((sock_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("client_sock: socket");
		return -1;
	}

	/* request connection to server */
	if (connect(sock_fd, (struct sockaddr *)&peer, sizeof(peer)) < 0) {
		perror("client_sock: connect");
		close(sock_fd);
		return -1;
	}

	return sock_fd;
}


int main_client_wait() {
	while(CHILD_COUNT != 0){
        pid_t pid;
        int status;
        if((pid = wait(&status)) == -1) {
            perror("main_client_wait: wait");
            return -1;
        } else {

            if(!WIFEXITED(status)){
                fprintf(stderr, "main_client_wait: wait return no status\n");
                return -1;
            } else if(WEXITSTATUS(status) != 0){
                fprintf(stderr, "main_client_wait: child %d \tterminated "
                        "with [%d] (error)\n", pid, WEXITSTATUS(status));
                return -1;
            }
        }
		CHILD_COUNT --;
    }
    return 0;
}


/**
 * Traverse the file rooted at src.
 * @param  sock_fd the socket file descriptor.
 * @return         0 on success; -1 on failure.
 */
int traverse(int sock_fd, char *src_path, char *server_path, char *host,
			 unsigned short port) {
	// first generate and send request
	struct request req;
	if (generate_request(sock_fd, src_path, server_path, &req) < 0) {
		fprintf(stderr, "traverse: generate_request\n");
		return -1;
	}
	printf("path: %s; type: %d; mode: %u; hash: %s; size: %u\n", req.path,
		   req.type, req.mode, req.hash, req.size);

	if (send_request(sock_fd, &req) < 0) {
		fprintf(stderr, "traverse: send_request\n");
		return -1;
	}

	// read the response to see if client should fork and send file
	int response = ERROR;
	if (read(sock_fd, &response, sizeof(int)) < 0) {
		perror("traverse: read");
		return -1;
	}
	response = ntohl(response);

	if (response == SENDFILE) {
		// fork a new process and send file
		int result = fork();
		CHILD_COUNT ++;
		if (result < 0) {
			perror("traverse: fork");
			return -1;
		} else if (result == 0) { // child
			// create a new socket
			sock_fd = client_sock(host, port);
			int file_type = req.type;
			req.type = TRANSFILE;
			if (send_request(sock_fd, &req) < 0) {
				fprintf(stderr, "traverse: send_request\n");
				exit(-1);
			}

			// only send data when the file is REGFILE and its size > 0
			if (file_type == REGFILE && req.size > 0) {
				if (send_data(sock_fd, src_path) < 0) {
					fprintf(stderr, "traverse: send_data %s\n", src_path);
					close(sock_fd);
					exit(-1);
				}
			}

			// send data or not, read another response from the server
			if (read(sock_fd, &response, sizeof(int)) < 0) {
				perror("traverse: read");
				close(sock_fd);
				return -1;
			}
			response = ntohl(response);

			close(sock_fd);

			if (response == OK) {
				exit(0);
			} else if (response == ERROR) {
				fprintf(stderr, "traverse child for %s: server read data",
						src_path);
			} else {
				fprintf(stderr,
						"traverse child for %s: server incorrect response\n",
						src_path);
			}
			exit(-1);
		}

	} else if (response == ERROR) {
		fprintf(stderr,
				"traverse: the server responded with ERROR on file %s\n",
				src_path);
		return -1;
	} else if (response != OK) {
		fprintf(stderr, "traverse: invalid response from server\n");
		return -1;
	}


	struct stat src_stat;
	if (lstat(src_path, &src_stat) != 0) {
		perror("traverse: lstat");
		exit(-1);
	}

	if (S_ISDIR(src_stat.st_mode)) {
		// if src_path is a dir then traverse recursively
		DIR *dirp;
		struct dirent *dirent;

		if (!(dirp = opendir(src_path))) {
			perror("sync_dir: opendir");
			return -1;
		}

		// traverse directory
		while ((dirent = readdir(dirp))) {
			// ignore . files
			if (strncmp(dirent->d_name, ".", 1) == 0) {
				continue;
			}

			// get new src path
			char new_src_path[MAXPATH];
			strncpy(new_src_path, src_path,
					sizeof(new_src_path) - strlen(src_path) - 1);
			strncat(new_src_path, "/", sizeof(new_src_path) - 1 - 1);
			strncat(new_src_path, dirent->d_name,
					sizeof(new_src_path) - strlen(dirent->d_name) - 1);
			// get new server path
			char new_server_path[MAXPATH];
			strncpy(new_server_path, server_path,
					sizeof(new_server_path) - strlen(server_path) - 1);
			strncat(new_server_path, "/", sizeof(new_server_path) - 1 - 1);
			strncat(new_server_path, dirent->d_name,
					sizeof(new_server_path) - strlen(dirent->d_name) - 1);

			if (traverse(sock_fd, new_src_path, new_server_path, host, port) <
				0) {
				fprintf(stderr, "traverse: traverse\n");
				return -1;
			}
		}
		closedir(dirp);
	} else if (!S_ISREG(src_stat.st_mode)) {
		fprintf(stderr, "traverse: Not supported file fomat\n");
		return -1;
	}


	return 0;
}

/**
 * Helper function that makes a request to the server to identify itself for
 * being the main client.
 * @param  sock_fd     the connecting socket file descriptor.
 * @param  src_path    the absolute or relative source path.
 * @param  server_path the server path.
 * @param  request	   the request to be filled in.
 * @return             0 on success, -1 on failure.
 */
static int generate_request(int sock_fd, char *src_path, char *server_path,
							struct request *request) {
	struct stat src_stat;

	if (lstat(src_path, &src_stat) != 0) {
		perror("generate_request: lstat");
		return -1;
	}

	strncpy(request->path, server_path, strlen(server_path) + 1);
	request->mode = src_stat.st_mode;
	request->size = src_stat.st_size;

	if (S_ISREG(src_stat.st_mode)) {
		// open file for hash
		FILE *src_f;
		if (!(src_f = fopen(src_path, "rb"))) {
			perror("generate_request: fopen");
			return -1;
		}

		hash(request->hash, src_f);
		request->type = REGFILE;

		if (fclose(src_f) < 0) {
			perror("generate_request: fclose");
			return -1;
		}
	} else if (S_ISDIR(src_stat.st_mode)) {
		strncpy(request->hash, "\0", BLOCKSIZE);
		request->type = REGDIR;
	} else {
		fprintf(stderr, "generate_request: Unsupported file type\n");
		return -1;
	}

	return 0;
}

/**
 * Helper function that sends the request struct to the server.
 * @param  sock_fd the connecting socket file descriptor.
 * @param  request the request struct that has been filled out by
 *                 generate_request.
 * @return         0 on success, -1 on failure.
 */
static int send_request(int sock_fd, struct request *request) {

	int type = htonl(request->type);
	if (write(sock_fd, &type, sizeof(int)) < 0) {
		perror("send_request: write type");
		return -1;
	}

	if (write(sock_fd, request->path, MAXPATH) < 0) {
		perror("send_request: write path");
		return -1;
	}

	mode_t mode = htons(request->mode);
	if (write(sock_fd, &mode, sizeof(mode_t)) < 0) {
		perror("send_request: write mode");
		return -1;
	}

	if (write(sock_fd, request->hash, BLOCKSIZE) < 0) {
		perror("send_request: write hash");
		return -1;
	}

	int size = htons(request->size);
	if (write(sock_fd, &size, sizeof(int)) < 0) {
		perror("send_request: write size");
		return -1;
	}

	return 0;
}


static int send_data(int sock_fd, char *src_path) {
	FILE *f;
	if (!(f = fopen(src_path, "rb"))) {
		perror("send_data: fopen");
		return -1;
	}

	char buf[MAXDATA];
	int num_read;
	while ((num_read = fread(buf, 1, MAXDATA, f)) > 0) {
		if (write(sock_fd, buf, num_read) < 0) {
			perror("send_data: write");
			if (fclose(f) != 0) {
				perror("send_data: fclose");
				return -1;
			}
			return -1;
		}
	}

	if (!feof(f)) {
		fprintf(stderr, "send_data: fread\n");
		if (fclose(f) != 0) {
			perror("send_data: fclose");
			return -1;
		}
		return -1;
	}

	if (fclose(f) != 0) {
		perror("send_data: fclose");
		return -1;
	}

	return 0;
}

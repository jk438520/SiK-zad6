// Patrz man basename, chcemy wersję GNU
#define _GNU_SOURCE

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "err.h"
#include "common.h"

// This is tcp server that handles multiple clients at the same time

// Program takes 1 argument: port number

// Client sends:
// 4 bytes: file lenght
// 2 bytes: file name lenght
// file name
// file

//Server receives file lenght, file name lenght, file naame
//prints "new client [client_ip:client:client_port] size=[file_size] file=[file_name]"
//waits 1 second
//receives file, saves it to file_name and prints:
//  client [client_ip:client:client_port] has sent its file of size=[file_size]
//   total size of uploaded files [sum of all file sizes]

// Server can handle multiple clients at the same time

#define QUEUE_LENGTH 5
#define BUFFER_SIZE 4096

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t total_read_len = 0;

void manage_connection(struct sockaddr_in client_addr, int client_fd) {
    uint32_t file_size;
    uint16_t file_name_size;
    char file_name[PATH_MAX];
    char buffer[BUFFER_SIZE];

    CHECK(read(client_fd, &file_size, sizeof(file_size)));
    file_size = ntohl(file_size);

    CHECK(read(client_fd, &file_name_size, sizeof(file_name_size)));
    file_name_size = ntohs(file_name_size);

    CHECK(read(client_fd, file_name, file_name_size));
    file_name[file_name_size] = '\0';

    printf("new client [%s:%d] size=%u file=%s\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port),
           file_size,
           file_name);

    sleep(1);
    //open file
    FILE *file = fopen(file_name, "w");
    if (file == NULL) {
        fatal("Cannot open file");
    }

    size_t read_len;
    size_t local_read_len = 0;
    do{
        read_len = receive_message(client_fd, buffer, BUFFER_SIZE, NO_FLAGS);
        total_read_len += read_len;
        fwrite(buffer, sizeof(char), read_len, file);
    }while(read_len > 0);

    fclose(file);

    printf("client [%s:%d] has sent its file of size=%zu\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port),
           local_read_len);

    pthread_mutex_lock(&mutex);
    total_read_len += local_read_len;
    printf("total size of uploaded files %zu\n", total_read_len);
    pthread_mutex_unlock(&mutex);
}


int main(int argc, char **argv) {

    if (argc != 2) {
        fatal("Usage: %s <port>", argv[0]);
    }

    struct sigaction action;
    sigset_t block_mask;

    sigemptyset (&block_mask);
    action.sa_handler = SIG_IGN;
    action.sa_mask = block_mask;
    action.sa_flags = 0;

    if (sigaction (SIGCHLD, &action, 0) == -1)
        fatal("sigaction");


    uint16_t port = read_port(argv[1]);

    int socket_fd = open_socket();
    bind_socket(socket_fd, port);

    start_listening(socket_fd, QUEUE_LENGTH);


    while (1) {
        struct sockaddr_in client_addr;
        int client_fd = accept_connection(socket_fd, &client_addr);

        switch (fork()) {
            case -1: // błąd
                fatal("fork");
                break;
            case 0:  // potomek
                close(socket_fd);
                manage_connection(client_addr, client_fd);
                exit(0);
            default:
                close(client_fd);
                // proces główny nie może wykonać wait()
        }
    }

    return 0;
}
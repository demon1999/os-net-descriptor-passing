#include <iostream>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <iostream>

const int CMAX = 2 * 1024, tries = 100;

void my_close(int socket, const char *name) {
    int status = close(socket);
    for (int i = 0; i < tries; i++) {
        if (status != -1 || errno != EINTR) {
            break;
        }
        status = close(socket);
    }
    if (status == -1) {
        fprintf(stderr, "Can't close %s socket: %s\n", name, strerror(errno));
        exit(0);
    }
}

char buf[2 * CMAX], rbuf[2 * CMAX];


//first argument directory
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Wrong usage: expected one argument!\n");
        return 0;
    }
    if (strlen(argv[1]) > 108) {
        fprintf(stderr, "Wrong usage: directory name is too long\n");
        return 0;
    }
    int client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_socket == -1) {
        fprintf(stderr, "Can't create a socket: %s\n", strerror(errno));
        return 0;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);
    int status = connect(client_socket, (struct sockaddr *) &addr, sizeof(addr));
    for (int i = 0; i < tries; i++) {
        if (status != -1 || errno != EINTR) {
            break;
        }
        status = connect(client_socket, (struct sockaddr *) &addr, sizeof(addr));
    }
    if (status == -1) {
        if (errno == EADDRINUSE || errno == EISCONN || errno == EADDRNOTAVAIL) {
            fprintf(stderr, "Error while connecting: %s\n", strerror(errno));
        } else if (errno == EACCES) {
            fprintf(stderr, "Permission denied: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Can't connect client socket: %s\n", strerror(errno));
        }
        return 0;
    }    

    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    union {         /* Ancillary data buffer, wrapped in a union
                      in order to ensure it is suitably aligned */
       char buf[CMSG_SPACE(2 * sizeof(int))];
       struct cmsghdr align;
    } u;
    int *fdptr;
    char iobuf[1];
    struct iovec io = {
       .iov_base = iobuf,
       .iov_len = sizeof(iobuf)
    };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);
    

    status = recvmsg(client_socket, &msg, 0);
    if (status == -1) {
        fprintf(stderr, "Can't receive fds: %s\n", strerror(errno));
        return 0;
    }
    
    int pipefdwrite = -1, pipefdread = -1;

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            pipefdwrite = reinterpret_cast<int*>(CMSG_DATA(cmsg))[0];
            pipefdread = reinterpret_cast<int*>(CMSG_DATA(cmsg))[1];
            break;
        }
    }

    if (pipefdread == -1 || pipefdwrite == -1) {
        fprintf(stderr, "Can't receive fds\n");
        return 0;
    }

    while (int cnt_read = read(0, buf, CMAX)) {  
        for (int i = 0; i < tries; i++) {
            if (cnt_read != -1 || errno != EINTR) {
                break;
            }
            cnt_read = read(0, rbuf, CMAX);
        }
        if (cnt_read == 0) {
            break;
        }
        if (cnt_read == -1) {
            fprintf(stderr, "Can't read from stdin: %s\n", strerror(errno));
            break;
        }

        int sum = 0;
        while (sum < cnt_read) {
            int cnt_write = write(pipefdwrite, buf + sum, cnt_read - sum);
            for (int i = 0; i < tries; i++) {
                if (cnt_write != -1 || errno != EINTR) {
                    break;
                }
                cnt_write = write(pipefdwrite, buf + sum, cnt_read - sum);
            }
            if (cnt_write == -1) {
                fprintf(stderr, "Can't write into socket: %s\n", strerror(errno));
                break;
            }
            bool err = false;
            int ssum = 0;            
            while (ssum < cnt_read) {
                int cnt = read(pipefdread, rbuf, CMAX);
                for (int i = 0; i < tries; i++) {
                    if (cnt != -1 || errno != EINTR) {
                        break;
                    }
                    cnt = read(pipefdwrite, rbuf, CMAX);
                }
                ssum += cnt;
                if (cnt == 0) {
                    break;
                }
                if (cnt == -1) {
                    fprintf(stderr, "Can't read from client socket: %s\n", strerror(errno));
                    err = true;
                    break;
                }
                for (int i = 0; i < cnt; i++) {
                    printf("%c", rbuf[i]);
                }
            }
            sum += cnt_write;
            if (err) {
                break;
            }
        }
        printf("\n");
    }
    my_close(pipefdread, "read_pipe");
    my_close(pipefdwrite, "write_pipe");
    my_close(client_socket, "client");   
    return 0;
} 
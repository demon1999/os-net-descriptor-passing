#include <signal.h>
#include <fcntl.h> 
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

const int CMAX = 1024, tries = 100;

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

char buf[2 * CMAX];

volatile int cp = 0;
void action(int sig) {
    cp = 1;
    signal(SIGINT, SIG_DFL);
}

struct sigaction sigact;

//first argument directory#include <signal.h>
int main(int argc, char **argv) {
    sigact.sa_flags = SA_SIGINFO;
    int status1 = sigemptyset(&sigact.sa_mask);
    if (status1 == -1) {
        fprintf(stderr, "Can't make empty set of signals: %s\n", strerror(errno));
        return 0;
    }
    sigact.sa_handler = action;
    status1 = sigaction(SIGINT, &sigact, NULL);
    if (status1 == -1) {
        fprintf(stderr, "Can't make sigaction call: %s\n", strerror(errno));
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "Wrong usage: expected one argument!\n");
        return 0;
    }
    if (strlen(argv[1]) > 108) {
        fprintf(stderr, "Wrong usage: directory name is too long\n");
        return 0;
    }
    int server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        fprintf(stderr, "Can't create a socket: %s\n", strerror(errno));
        return 0;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);
    int status = bind(server_socket, (struct sockaddr *) &addr, sizeof(addr));
    if (status == -1) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "Error while binding: %s\n", strerror(errno));
        } else if (errno == EINVAL || errno == ENOENT || errno == ENOTDIR) {
            fprintf(stderr, "Wrong directory name: %s\n", strerror(errno));
        } else if (errno == EACCES) {
            fprintf(stderr, "Permission denied: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Can't bind address to server socket: %s\n", strerror(errno));
        }
        return 0;
    }
    status = listen(server_socket, 50);
    if (status == -1) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "Error while listening: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Can't listen server socket: %s\n", strerror(errno));
        }
        return 0;
    }
    socklen_t addr_size = sizeof(addr);
    while (int client_socket = accept(server_socket, (struct sockaddr *) &addr, &addr_size)) {
        if (cp == 1) break;
        for (int i = 0; i < tries; i++) {
            if (client_socket != -1 || errno != EINTR) {
                break;
            }
            client_socket = accept(server_socket, (struct sockaddr *) &addr, &addr_size);
        }
        if (client_socket == -1) {
            fprintf(stderr, "Can't accept: %s\n", strerror(errno));
            return 0;
        }
        int pipefdread[2], pipefdwrite[2];
        for (int i = 0; i < 2; i++)
            pipefdread[i] = pipefdwrite[i] = -1;
        status = pipe(pipefdread);
        if (status == -1) {
            fprintf(stderr, "Can't create pipe for read: %s\n", strerror(errno));
            continue;
        }
        status = pipe(pipefdwrite);
        if (status == -1) {
            fprintf(stderr, "Can't create pipe for write: %s\n", strerror(errno));
            continue;
        }
        //Add errors

        struct msghdr msg = { 0 };
        struct cmsghdr *cmsg;

        int *fdptr;
        char iobuf[1];
        struct iovec io = {
           .iov_base = iobuf,
           .iov_len = sizeof(iobuf)
        };
        union {         /* Ancillary data buffer, wrapped in a union
                          in order to ensure it is suitably aligned */
           char buf[CMSG_SPACE(2 * sizeof(int))];
           struct cmsghdr align;
        } u;

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = u.buf;
        msg.msg_controllen = sizeof(u.buf);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);
        fdptr = (int *) CMSG_DATA(cmsg);    /* Initialize the payload */
        fdptr[0] = pipefdread[1];
        fdptr[1] = pipefdwrite[0];
        int status = sendmsg(client_socket, &msg, 0);
        my_close(pipefdread[1], "write pipe for client");
        my_close(pipefdwrite[0], "read pipe for client");
        if (status == -1) {
            fprintf(stderr, "Can't send fds: %s\n", strerror(errno));
            my_close(pipefdread[0], "read_pipe");
            my_close(pipefdwrite[1], "write_pipe");
            my_close(client_socket, "client");
            continue;
        }
        while (int cnt_read = read(pipefdread[0], buf, CMAX)) {
            for (int i = 0; i < tries; i++) {
                if (cnt_read != -1 || errno != EINTR) {
                    break;
                }
                cnt_read = read(pipefdread[0], buf, CMAX);
            }
            if (cnt_read == 0) {
                break;
            }
            if (cnt_read == -1) {
                fprintf(stderr, "Can't read from client socket: %s\n", strerror(errno));
                break;
            }
            int sum = 0;
            bool err = false;
            while (sum != cnt_read) {
                int cnt_write = write(pipefdwrite[1], buf + sum, cnt_read - sum);
                for (int i = 0; i < tries; i++) {
                    if (cnt_write != -1 || errno != EINTR) {
                        break;
                    }
                    cnt_write = write(pipefdwrite[1], buf + sum, cnt_read - sum);
                }
                if (cnt_write == -1) {
                    fprintf(stderr, "Can't write into client socket: %s\n", strerror(errno));
                    err = true;
                    break;
                }
                sum += cnt_write;
            }
            if (err) {
                break;
            }
        }
        my_close(pipefdread[0], "read_pipe");
        my_close(pipefdwrite[1], "write_pipe");
        my_close(client_socket, "client");
    }
    my_close(server_socket, "server");   
    status = unlink(argv[1]);
    if (status == -1) {
        fprintf(stderr, "Can't unlink socket: %s\n", strerror(errno));
    }
    return 0;
}
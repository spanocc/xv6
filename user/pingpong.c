#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int fd1[2], fd2[2];
    if(pipe(fd1) < 0 || pipe(fd2) < 0) {
        printf("pipe error!\n");
        exit(1);
    }
    int ret = -1;
    ret = fork();
    if(ret < 0) {
        printf("fork error!\n");
        exit(1);
    }

    char msg_byte = 'A';
    if(ret > 0) {
        close(fd1[0]);
        close(fd2[1]);

        write(fd1[1], &msg_byte, 1);
        if(read(fd2[0], &msg_byte, 1)) {
            printf("%d: received pong\n", getpid());
        }

        close(fd1[1]);
        close(fd2[0]);
        if(wait(0) != ret) {
            printf("wait error!\n");
        }

        exit(0);
    }else {
        close(fd1[1]);
        close(fd2[0]);

        if(read(fd1[0], &msg_byte, 1) > 0) {
            printf("%d: received ping\n", getpid());
        }
        write(fd2[1], &msg_byte, 1);

        close(fd1[0]);
        close(fd2[1]);
        exit(0);
    }

}
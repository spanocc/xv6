#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/* 不加这条还是会编译出错，无限递归 */
void process_handler(int *fd) __attribute__((noreturn));
// 一个进程负责一个素数的筛选  
void process_handler(int *fd) {
    int prime = 0;
    int tmp = 0;
    int ret = 0;
    int p[2] = {-1, -1};

    while((ret = read(fd[0], &tmp, sizeof(int))) > 0) {
        if(prime == 0) {
            prime = tmp; //初始的素数（本进程负责的）
            printf("prime %d\n", prime);
            continue;
        }
        if(tmp % prime == 0) {  //筛掉该素数
            continue;
        }

        if(p[0] == -1) {        //没有下一个进程，说明需要新创建一个进程
            if(pipe(p) < 0) {
                printf("pipe error!\n");
                exit(1);
            }
            if(fork() > 0) {
                close(p[0]);
                write(p[1], &tmp, sizeof(int));
            }
            else {
                close(p[1]);
                close(fd[0]);
                process_handler(p);
            }
        }
        else {                  // 正常传递数
            write(p[1], &tmp, sizeof(int));
        }

    }


    if(ret < 0) {
        printf("read error!\n");
        exit(1);
    }
    close(fd[0]); //关闭读
    if(p[1] != -1) close(p[1]); //关闭写
    
    wait(0);
    exit(0);

}


int main(int argc, char *argv[]) {
    int fd[2];
    if(pipe(fd) < 0) {
        printf("pipe error!\n");
        exit(1);
    }
    if(fork() > 0) {
        close(fd[0]);
        for(int i = 2; i <= 35; i++) {
            write(fd[1], &i, sizeof(int));
        }
        close(fd[1]);
        wait(0);
    }
    else {
        close(fd[1]);
        process_handler(fd);
    }
    exit(0);
}

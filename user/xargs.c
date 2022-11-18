#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define BUFSIZE 512

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(2, "xargs: arguments are too less\n");
        exit(1);
    }
    if(argc > MAXARG - 1) {
        fprintf(2, "xargs: arguments are too much\n");
        exit(1);
    }
    char *margv[MAXARG];
    char buf[BUFSIZE];
    int ret = -1;
    int dex = 0;
    while(1) {
        if(dex >= BUFSIZE - 1) {
            fprintf(2, "xargs: argument is too long\n");
            break;
        }
        if((ret = read(0, buf + dex, 1)) <= 0) {
            break;
        }
        if(buf[dex] == '\n') {
            buf[dex] = '\0';
            margv[0] = argv[1];
            int i = 1;
            for(; i+1 < argc; ++i) {
                margv[i] = argv[i+1];
            } 
            margv[i++] = buf;
            margv[i] = 0; //空指针
            if(fork() == 0) {
                exec(margv[0], margv);
                //assert(0);
                fprintf(2, "xargs: exec failue\n");
                break;
            }
            else {
                wait(0); //等子进程完成后再下一步
                dex = 0;
            }
        }
        else dex ++;
    }
    exit(0);
}
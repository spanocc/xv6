#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 只找文件，不找目录
void myfind(char *path, char *filename) {   //printf("%s %s\n",path, filename);
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    if(st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      fprintf(2, "find: path too long\n");
      return;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if(de.inum == 0)   //还不知道什么意思，不过ls.c文件里也有
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }
        if(st.type == T_FILE && (!strcmp(p, filename))) {
            printf("%s\n", buf);
        }  
        if(st.type == T_DIR && strcmp(p, ".") && strcmp(p, "..")) {
            myfind(buf, filename);
        }
    }
    close(fd);

}


int main(int argc, char *argv[]) {
    if(argc < 3) {
        printf("arguments are too less!\n");
        exit(1);
    }

    myfind(argv[1], argv[2]);
    exit(0);
}
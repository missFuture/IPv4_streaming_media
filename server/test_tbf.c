#include <stdio.h>
#include <stdlib.h>
#include "mytbf.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#define CPS 5
#define BUFFERSIZE 1024
#define BURST 5
int main(int argc, char * argv[])
{
    int sfd;
    int dfd = 1;
    char buf[BUFFERSIZE];
    int read_return;
    int write_return;
    int token_get;
    int pos = 0;
    mytbf_t *tbf;
    if(argc < 2)
    {
        fprintf(stderr, "Usage: a.out <pathname>\n");
        exit(1);
    }
    tbf = mytbf_init(CPS, BURST);
    if(tbf == NULL)
    {
        fprintf(stderr, "%s", strerror(errno));
        exit(1);
    }
    sfd = open(argv[1], O_RDONLY);
    if(sfd < 0)
    {
        perror("open()");
        exit(1);
    }
    while(1)
    {
        token_get = mytbf_fetchtoken(tbf, BUFFERSIZE);
        read_return = read(sfd, buf, token_get);
        if(read_return < 0)
        {
            perror("read error()");
            break;
        }
        if(read_return == 0)
            break;
        pos = 0;
        if(token_get - read_return > 0)
            mytbf_returntoken(tbf, token_get - read_return);
        while(read_return > 0)
        {
            write_return = write(dfd, buf+pos, read_return);
            if(write_return < 0)
            {
                perror("write error");
                exit(1);
            }
            pos += write_return;
            read_return -= write_return;
        }
        memset(buf, 0, BUFFERSIZE);
    }

    close(sfd);
    close(dfd);
    mytbf_destroy(tbf);
    exit(0);
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    int parent_fd[2];
    int child_fd[2];
    char *ping = "ping";
    char *pong = "pong";
    char buffer[8]; // store "ping" or "pong", at least 5 bytes

    if (argc != 1)
    {
        fprintf(2, "usage: pingpong\n");
        exit(-1);
    }
    pipe(parent_fd);
    pipe(child_fd);
    if (fork() == 0)
    {
        if (read(parent_fd[0], (void *)buffer, sizeof(buffer)) > 0)
        {
            write(child_fd[1], (void *)pong, sizeof(pong));
            printf("%d: received %s\n", getpid(), buffer);
        }
        else
        {
            fprintf(2, "read error!\n");
        }
        exit(0);
    }
    write(parent_fd[1], (void *)ping, sizeof(ping));
    if (read(child_fd[0], (void *)buffer, sizeof(buffer)) > 0)
    {
        printf("%d: received %s\n", getpid(), buffer);
    }
    else
    {
        fprintf(2, "read error!\n");
    }
    exit(0);
}

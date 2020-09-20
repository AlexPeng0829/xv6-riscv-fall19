#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void next_child(int *parent_fds)
{
    int buf;
    close(parent_fds[1]);

    if (read(parent_fds[0], (void *)&buf, sizeof(int)) > 0)
    {
        int fd_pair[2];
        int first_prime = buf;

        printf("prime %d\n", first_prime);
        pipe(fd_pair);

        if (fork() == 0)
        {
            next_child(fd_pair);
        }

        close(fd_pair[0]);
        while (read(parent_fds[0], (void *)&buf, sizeof(int)) > 0)
        {
            if (buf % first_prime != 0)
            {
                write(fd_pair[1], (void *)&buf, sizeof(int));
            }
        }
        close(parent_fds[0]);
        close(fd_pair[1]);
        if (wait(0) < 0)
        {
            fprintf(2, "wait error!\n");
        }
        exit(-1);
    }

    exit(0);
}

int main(int argc, char **argv)
{
    int parent_fds[2];
    if (argc != 1)
    {
        fprintf(2, "usage: primes\n");
        exit(-1);
    }
    pipe(parent_fds);

    if (fork() == 0)
    {
        next_child(parent_fds);
    }

    close(parent_fds[0]);
    printf("prime 2\n");
    for (int j = 2; j < 36; ++j)
    {
        if (j % 2 != 0)
            write(parent_fds[1], (void *)&j, sizeof(int));
    }
    close(parent_fds[1]);
    if (wait(0) < 0)
    {
        fprintf(2, "wait error!\n");
        exit(-1);
    }
    exit(0);
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    int idx = 0;
    int i = 0;
    int j = 1;
    char buf;
    char input_argv_storage[MAXARG][64];
    char *input_argv[MAXARG];
    for (int index = 0; index < MAXARG; ++index)
    {
        input_argv[index] = input_argv_storage[index];
    }
    if (argc == 1)
    {
        fprintf(2, "usage: xargs expects extra arguments\n");
        exit(-1);
    }
    if (argc > MAXARG)
    {
        fprintf(2, "error: too many arguments for xargs\n");
        exit(-1);
    }
    for (; j < argc; ++j)
    {
        memmove((void *)input_argv[j - 1], argv[j], strlen(argv[j]));
    }
    idx = j - 1;
    while (read(0, &buf, 1) > 0)
    {
        if (buf == ' ' || buf == '\n')
        {
            input_argv[idx][i] = '\0';
            idx++;
            i = 0;
        }
        else
        {
            input_argv[idx][i++] = buf;
        }
    }

    if (idx + argc > MAXARG - 1)
    {
        fprintf(2, "Error: input arguments exceed maximum allowed!\n");
        exit(-1);
    }
    input_argv[idx + 1] = (char *)0; //mark end of input arg


    if (fork() == 0)
    {
        exec(argv[1], (char **)(input_argv));
        exit(0);
    }
    if (wait(0) < 0)
    {
        fprintf(2, "Wait error!\n");
        exit(-1);
    }
    exit(0);
}

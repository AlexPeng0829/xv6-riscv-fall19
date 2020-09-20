/* N-Shell.
simple shell that supports limited commands which includes:

1. single pipe: |
2. io redirection: <, > only support command before pipe!!!

*/

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// clang-format off
#define MAXARGS     10
#define WORD_NUM    16

struct Cmd
{
    char argv_storage[MAXARGS][WORD_NUM];
    char *argv[MAXARGS];
    int curr_argv_idx;                    // idx points to the argv to be filled, called and modified by advance_argv_ptr()
    int is_piped;                         // flag to indicate whether it is one of the piped command or not
    int fd_in;                            // redirect stdin to fd_in if < is provided
    int fd_out;                           // redirect fd_out to stdout if > is provided

};
// clang-format on

void init_cmd(struct Cmd *cmd)
{
    cmd->fd_in = 0;
    cmd->fd_out = 1;
    cmd->is_piped = 0;
    cmd->curr_argv_idx = 0;
    for (int i = 0; i < MAXARGS; ++i)
    {
        (cmd->argv)[i] = &(cmd->argv_storage)[i][0];
        memset((cmd->argv)[i], 0, WORD_NUM);
    }

    return;
};


int get_cmd(char *buf, int nbuf)
{
    fprintf(2, "@ ");
    memset(buf, 0, nbuf);
    //TODO: it seems gets() is wrongly implemented?
    //TODO: as it always insert \n before end of string token '\0'
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
        return -1;
    buf[strlen(buf) - 1] = '\0';
    return 0;
}

int Fork(void)
{
    int pid;

    pid = fork();
    if (pid == -1)
    {
        fprintf(2, "fork error\n");
        exit(-1);
    }
    return pid;
}

void view_cmd(struct Cmd *cmd)
{

    int i = 0;
    fprintf(2, "------------------cmd-------------------\n");
    fprintf(2, "cmd->execute_path: %s\n", cmd->argv[0]);
    fprintf(2, "cmd->is_piped: %d\n", cmd->is_piped);
    fprintf(2, "cmd->fd_in: %d\n", cmd->fd_in);
    fprintf(2, "cmd->fd_out: %d\n", cmd->fd_out);
    fprintf(2, "cmd->argv:\n");
    while (cmd->argv[i])
    {
        fprintf(2, "%s\n", cmd->argv[i]);
        i++;
    }
    fprintf(2, "------------------cmd-------------------\n");
}

/*
get next word seperated by consecutive spaces
@return: the index points to che char to be pasrsed
*/
int get_next_word(char *input, int idx, char *buf, int size)
{
    while (input[idx] == ' ')
    {
        idx++;
    }
    for (int i = 0;; ++i)
    {
        buf[i] = input[idx++];
        if (buf[i] == ' ' || buf[i] == '\0')
        {
            buf[i] = '\0';
            return idx;
        }
        if (i > size)
        {
            fprintf(2, "error, input argv size(%d) exceeds maximum word size(%d)\n", i, WORD_NUM);
            return -1;
        }
    }
}

/*
increment the curr_argv_idx to next empty argv_storage
@return: pointer to next argv_storage
*/
char *advance_argv_ptr(struct Cmd *cmd)
{
    cmd->curr_argv_idx++;
    if (cmd->curr_argv_idx > MAXARGS)
    {
        fprintf(2, "error, input argv number exceeds maximum number(%d)!\n", MAXARGS);
        return (char *)0;
    }
    return cmd->argv[cmd->curr_argv_idx];
}

int parse_cmd(char *s, struct Cmd *first_cmd, struct Cmd *second_cmd)
{
    struct Cmd *curr_parsing_cmd = first_cmd;
    char *argv_ptr = first_cmd->argv[0];
    int i = 0;

    while (i < strlen(s))
    {
        // skip spaces
        if (s[i] == ' ')
        {
            ++i;
        }

        // get the argv, set up Cmd if there is '<', '>', '|'
        else
        {
            char next_word[WORD_NUM];
            memset(next_word, 0, WORD_NUM);
            switch (s[i])
            {
            case '<':
                ++i;
                if ((i = get_next_word(s, i, next_word, WORD_NUM)) == -1)
                {
                    return -1;
                }
                curr_parsing_cmd->fd_in = open(next_word, 0);
                close(0);
                dup(curr_parsing_cmd->fd_in); // fd_in and 0 refer to same file now
                break;
            case '>':
                ++i;
                if ((i = get_next_word(s, i, next_word, WORD_NUM)) == -1)
                {
                    return -1;
                }
                curr_parsing_cmd->fd_out = open(next_word, O_CREATE | O_WRONLY);
                close(1);
                dup(curr_parsing_cmd->fd_out); // fd_out and 1 refer to same file now
                break;
            case '|':
                ++i;
                if ((i = get_next_word(s, i, next_word, WORD_NUM)) == -1)
                {
                    return -1;
                }
                curr_parsing_cmd->argv[curr_parsing_cmd->curr_argv_idx] = (void *)0;

                curr_parsing_cmd = second_cmd;
                argv_ptr = (second_cmd->argv)[0];
                memmove(argv_ptr, next_word, WORD_NUM);
                argv_ptr = advance_argv_ptr(curr_parsing_cmd);

                first_cmd->is_piped = 1;
                second_cmd->is_piped = 1;
                break;
            default:
                if ((i = get_next_word(s, i, argv_ptr, WORD_NUM)) == -1)
                {
                    return -1;
                }
                argv_ptr = advance_argv_ptr(curr_parsing_cmd);
            }
        }
    }
    curr_parsing_cmd->argv[curr_parsing_cmd->curr_argv_idx] = (void *)0;
    return 0;
}

void run_cmd(struct Cmd *first_cmd, struct Cmd *second_cmd)
{
    // view_cmd(first_cmd);
    // if (first_cmd->is_piped)
    // {
    //     view_cmd(second_cmd);
    // }

    if (first_cmd->is_piped && second_cmd->is_piped)
    {
        int fd_piped[2];
        pipe(fd_piped);

        if (Fork() == 0)
        {

            close(first_cmd->fd_out);
            dup(fd_piped[1]);

            close(fd_piped[0]);
            close(fd_piped[1]);

            if (exec((first_cmd->argv)[0], (char **)&first_cmd->argv) == -1)
            {
                fprintf(2, "error executing first_cmd of pipe\n");
                exit(-1);
            }
        }
        if (Fork() == 0)
        {

            close(second_cmd->fd_in);
            dup(fd_piped[0]);

            close(fd_piped[0]);
            close(fd_piped[1]);

            if (exec((second_cmd->argv)[0], (char **)&second_cmd->argv) == -1)
            {
                fprintf(2, "error executing second_cmd of pipe\n");
                exit(-1);
            }
        }
        close(fd_piped[0]);
        close(fd_piped[1]);
        wait(0);
        wait(0);
    }
    else
    {
        exec((first_cmd->argv)[0], (char **)&first_cmd->argv);
        fprintf(2, "error executing first_cmd\n");
        exit(-1);
    }
}

int main(void)
{
    char buf[100];
    struct Cmd first_cmd;
    struct Cmd second_cmd;
    int fd;

    // Ensure that three file descriptors are open.
    while ((fd = open("console", O_RDWR)) >= 0)
    {
        if (fd >= 3)
        {
            close(fd);
            break;
        }
    }

    // Read and run input commands.
    while (get_cmd(buf, sizeof(buf)) >= 0)
    {

        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
        {
            // Chdir must be called by the parent, not the child.
            buf[strlen(buf) - 1] = 0; // chop \n
            if (chdir(buf + 3) < 0)
                fprintf(2, "cannot cd %s\n", buf + 3);
            continue;
        }

        if (Fork() == 0)
        {
            init_cmd(&first_cmd);
            init_cmd(&second_cmd);
            if (parse_cmd(buf, &first_cmd, &second_cmd) == 0)
            {
                run_cmd(&first_cmd, &second_cmd);
                exit(0);
            }
        }

        if (wait(0) < 0)
        {
            fprintf(2, "wait error!\n");
            exit(0);
        }
    }
    exit(0);
}
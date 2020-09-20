#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *file)
{
    int fd;
    char buf[512];
    char *p;
    struct dirent de;
    struct stat st;
    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s!\n", path);
        return;
    }
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s!\n", path);
        close(fd);
    }
    switch (st.type)
    {
    case T_FILE:
        if (!strcmp(path, file))
        {
            printf("%s\n", path);
        }
        close(fd);
        return;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf))
        {
            printf("find: path too long!\n");
            return;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue;
            if(!strcmp(".", de.name) || !strcmp("..", de.name)){
                continue;
            }
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            // printf("buf: %s\n", buf);
            if (stat(buf, &st) < 0)
            {
                printf("find: cannot stat %s!\n", buf);
                continue;
            }
            switch (st.type)
            {
            case T_DIR:
                find(buf, file);
                break;
            case T_FILE:
            //TODO: 1. current match only try to match "some_file", need to extend to match "some_dir/some_file"
            //TODO: 2. ls is not correctly implemented, when we cd to child dir, ls execution will fail
            //TODO: 3. extend to support regular expression match
                if (!strcmp(de.name, file))
                {
                    printf("%s\n", buf);
                }
                break;
            }
        }
    }
    close(fd);
    return;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(2, "usage: find <dir> <file>\n");
        exit(-1);
    }
    find(argv[1], argv[2]);
    exit(0);
}

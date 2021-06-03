#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define NPAGES 20

void test_read_write(char *s)
{
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = 0; i < NPAGES; i++)
    {
        if (alloc[i * PGSIZE] != 'a' + i)
        {
            exit(1);
        }
    }
    free(alloc);
}

void fork_test(char *s)
{
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    int pid = fork();
    if (pid == 0)
    {
        for (int i = 0; i < NPAGES; i++)
        {
            if (alloc[i * PGSIZE] != 'a' + i)
            {
                exit(-5);
            }
        }
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
        if (status == -5)
        {
            exit(1);
        }
    }
    else
    {
        exit(1);
    }
    free(alloc);
}

void full_swap_test(char *s)
{
    int proc_size = ((uint64)sbrk(0)) & 0xFFFFFFFF;
    int allocsize = 32 * PGSIZE - proc_size;
    char *alloc = sbrk(allocsize);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    sbrk(-allocsize);
}

void benchmark(char *s)
{
    int up = uptime();
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
        sleep(1);
    }
    for (int i = NPAGES - 1; i >= 0; i--)
    {
        if (alloc[i * PGSIZE] != 'a' + i)
        {
            exit(1);
        }
        alloc[i * PGSIZE] = 'a' + i;
        sleep(1);
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
        sleep(1);
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
        sleep(1);
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
        sleep(1);
    }
    free(alloc);
    int **array = (int **)malloc(100 * sizeof(int *));
    for (int i = 0; i < 100; i++)
    {
        array[i] = malloc(100 * sizeof(int));
        sleep(1);
    }
    for (int i = 0; i < 100; i++)
    {
        for (int j = 0; j < 100; j++)
        {
            array[i][j] = 0;
        }
        sleep(1);
    }
    for (int j = 0; j < 100; j++)
    {
        for (int i = 0; i < 100; i++)
        {
            array[i][j] = 0;
        }
        sleep(1);
    }
    for (int i = 0; i < 100; i++)
    {
        free(array[i]);
        sleep(1);
    }
    free(array);
    printf("total time: %d\n", uptime() - up);
}

void segmentation_fault_test(char *s)
{
    int pid = fork();
    if (pid == 0)
    {
        char *alloc = malloc(NPAGES * PGSIZE);
        alloc[(NPAGES + 7) * PGSIZE] = 'a';
        exit(0);
    }
    else
    {
        int status;
        wait(&status);
        if (status >= 0)
        {
            exit(1);
        }
        else if (status < 0)
        {
            return;
        }
    }
}

// void pagefaults_test(char *s)
// {
//     int pagefaults = 0;
//     char *alloc = malloc(NPAGES * PGSIZE);
//     for (int i = 0; i < NPAGES; i++)
//     {
//         alloc[i * PGSIZE] = 'a' + i;
//     }
//     if (pagefaults == get_pagefaults())
//     {
//         exit(1);
//     }
//     pagefaults = get_pagefaults();
//     for (int i = 0; i < NPAGES; i++)
//     {
//         if (alloc[i * PGSIZE] != 'a' + i)
//         {
//             exit(1);
//         }
//     }
//     if (pagefaults == get_pagefaults())
//     {
//         exit(1);
//     }
// }

int run(void f(char *), char *s)
{
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if ((pid = fork()) < 0)
    {
        printf("runtest: fork error\n");
        exit(1);
    }
    if (pid == 0)
    {
        f(s);
        exit(0);
    }
    else
    {
        wait(&xstatus);
        if (xstatus != 0)
            printf("FAILED\n");
        else
            printf("OK\n");
        return xstatus == 0;
    }
}

int main(int argc, char *argv[])
{
    struct test
    {
        void (*f)(char *);
        char *s;
    } tests[] = {
        {test_read_write, "read_write_test"},
        {fork_test, "fork_test"},
        {full_swap_test, "full_swap_test"},
        {segmentation_fault_test, "segmentation_fault_test"},
        // {pagefaults_test, "pagefault_test"},
        {benchmark, "benchmark"},
        {0, 0},
    };

    printf("sanity tests starting\n");
    int fail = 0;
    for (struct test *t = tests; t->s != 0; t++)
    {
        if (!run(t->f, t->s))
            fail = 1;
    }
    if (fail)
    {
        printf("SOME TESTS FAILED\n");
        exit(1);
    }
    else
    {
        printf("ALL TESTS PASSED\n");
        exit(0);
    }
}
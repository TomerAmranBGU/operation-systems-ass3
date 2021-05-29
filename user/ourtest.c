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

void write_read_test(char* name){
    char *alloc = malloc(NPAGES * PGSIZE);
    for (char i = 0; i < NPAGES; i++){
        alloc[i * PGSIZE] = i;
    }
    for (char i = 0; i < NPAGES; i++){
        printf("should be [%d]: %d\n", i , alloc[i * PGSIZE]);
        if (alloc[i * PGSIZE] != i)
            exit(1);
    }
    free(alloc);
}
void fork_copy_test(char* name){
    char *alloc = malloc(NPAGES * PGSIZE);
    for (char i = 0; i < NPAGES; i++){
        alloc[i * PGSIZE] = i;
    }
    int pid = fork();
    if (pid ==0){
        for (char i = 0; i < NPAGES; i++){
            printf("should be [%d]: %d\n", i , alloc[i * PGSIZE]);
            if (alloc[i * PGSIZE] != i)
            exit(-6);
        }
    }
    else{
        int status;
        wait(&status);
        if (status == -6)
        {
            exit(1);
        }
    }
    free(alloc);
}
void allocating_max_allowed_pages(char *s)
{
    int inital_size = ((uint64) sbrk(0)) & 0xFFFFFFFF;
    int post_alloc_size = 32 * PGSIZE - inital_size;
    char *alloc = sbrk(post_alloc_size);
    for (char i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] =  i;
    }
    for (char i = 0; i < NPAGES; i++)
    {
        if (alloc[i * PGSIZE] !=  i)
            exit(1);
    }
    sbrk(-post_alloc_size);
}
void dealloc_test(char* s){
    char * alloc = sbrk(NPAGES *PGSIZE);
    for (char i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] =  i;
    }
    sbrk(-NPAGES *PGSIZE);
    printf("should cause segmentation fault\n");
    for (char i = 0; i < NPAGES; i++)
    {
        if (alloc[i * PGSIZE] !=  i)
            exit(1);
    }
}
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
void performance_serial(char* s){
    int start = uptime();
    char *alloc = malloc(NPAGES*PGSIZE);
    for (int j =0 ; j<10; j++){
        for (char i = 0; i < NPAGES; i++) {
            alloc[i * PGSIZE] = i;
        }
        for (char i = 0; i < 20; i++){
            if (alloc[i * PGSIZE] != i) {
                exit(1);
            }
        }
    }
    free(alloc);
    printf("total time: %d\n",uptime() - start);
}
void performance_random_access(char* s){
    int start = uptime();
    char *alloc = malloc(20*PGSIZE);
    char rand[] = { 5, 12,  8,  9, 16, 17,  3, 16, 12,  5,  2,  6,  2, 18, 14, 18,  8,
       10,  1, 16,  4,  1,  7, 15,  8, 16, 12, 14, 14,  4,  8, 14,  7,  1,
       14,  3,  1, 14, 14, 11,  8, 18, 13, 18, 19,  6,  3, 10, 19,  4,  6,
        7, 17,  2,  1,  6, 14,  8,  5,  6,  1, 16, 11, 12, 10,  4,  2, 19,
       15, 14,  2,  2,  3,  8,  3, 13,  6, 10,  3,  2, 12, 10, 12,  7,  3,
       12,  6,  6, 18,  6,  7,  5, 18,  0,  8, 14, 11,  7,  3,  1};
    for (char i = 0; i < 20; i++) {
        alloc[i * PGSIZE] = i;
    }
    for (int j =0 ; j<10; j++){
        for (int i =0 ; i<100; i++){
            if (alloc[rand[i]*PGSIZE] != rand[i]){
                exit(1);
            }
        }
    }
    free(alloc);
    printf("total time: %d\n",uptime() - start);
}
int main(int argc, char *argv[])
{
    struct test
    {
        void (*f)(char *);
        char *s;
    } tests[] = {
        {write_read_test, "write_read_test"},
        {fork_copy_test, "fork_copy_test"},
        {allocating_max_allowed_pages, "full_swap_test"},
        {dealloc_test,"dealloc_test"},
        {performance_serial, "performance_serial"}, 
        {performance_random_access, "performance_random_access"}, 
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
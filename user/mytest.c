
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
// int can_smbrk_many(int n){
//   fprintf(2, "cansmbrk_many\n");
//   char* arr[n];
//   for (int i=0; i<n; i++){
//       arr[i] = sbrk(4096);
//   }
//    for (int i=0; i<n; i++){
//       printf("%d\n", arr[i][0]);
//   }
//   return 0;
// }
// int smbrk_once(){
//   fprintf(2, "cansmbrk_once\n");
//   for (int i=0; i< 1; i++){
//       sbrk(4096);
//   }
//   return 0;
// }
// #define REGION_SZ (20*PGSIZE)

// void
// sparse_memory(int pagenum)
// {
//   char *i, *prev_end, *new_end;
  
//   prev_end = sbrk(PGSIZE*pagenum);
//   if (prev_end == (char*)0xffffffffffffffffL) {
//     printf("sbrk() failed\n");
//     exit(1);
//   }
//   printf("sbrk() successed\n");
//   new_end = prev_end + PGSIZE*pagenum;

//   for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE)
//     *(char **)i = i;

//   for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE) {
//     if (*(char **)i != i) {
//       printf("failed to read value from memory\n");
//       exit(1);
//     }
//   }

//   exit(0);
// }
// int
// main(int argc, char *argv[])
// {
//   // smbrk_once();
//   can_smbrk_many(atoi(argv[1]));
  
//   // sparse_memory(atoi(argv[1]));
//   exit(0);
// }
#define PG_SIZE 4096
#define PG_NUM 20

void read_write_test(char *pages){
    printf("write to %d pages \n", PG_NUM);
    for (int i = 0; i < PG_NUM; i++){
        pages[i * PG_SIZE] = i;
    }

    // printf("read from page 0 (in disk) \n");
    // printf("data read: %d\n", pages[0]);
    printf("read from %d pages \n", PG_NUM);
    for (int i = 0; i < PG_NUM; i++){
        printf("data read: %d\n", pages[i * PG_SIZE]);
    }
}
int main()
{
    char *pages = malloc(PG_SIZE * PG_NUM);
    sleep(1);
    printf("Task 3: Starting Tests\n---\n");
    sleep(1);
    read_write_test(pages);
    printf("Freed all pages\n");
    exit(0);
}
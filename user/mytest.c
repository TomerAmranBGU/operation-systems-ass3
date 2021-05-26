#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int can_smbrk_many(){
  fprintf(2, "cansmbrk_many\n");
  for (int i=0; i< 14; i++){
      sbrk(4096);
  }
  return 0;
}
int smbrk_once(){
  fprintf(2, "cansmbrk_once\n");
  for (int i=0; i< 1; i++){
      sbrk(4096);
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  // smbrk_once();
  can_smbrk_many();
  exit(0);
}

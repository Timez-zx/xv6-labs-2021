#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
  if(argc < 2){
      int pid = wait((int*)0);
      if(pid >= 0)
         printf("PID: %d finished\n", pid);
  }
  else if(argc == 2){
      int arg = atoi(argv[1]);
      wait(&arg);
  }
  else{
      fprintf(2, "Error: wait PID");
  }
  exit(0);
}
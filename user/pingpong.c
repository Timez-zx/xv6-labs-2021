#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int pipe_fd0[2];
    int pipe_fd1[2];
    if(pipe(pipe_fd0) == -1){
        printf("Pipe Failed\n");
        exit(-1);
    }
    if(pipe(pipe_fd1) == -1){
        printf("Pipe Failed\n");
        exit(-1);
    }
    int son_proc = fork();
    if(son_proc == 0){
        int pid = getpid();
        char receive[1];
        close(pipe_fd0[1]);
        read(pipe_fd0[0], receive, 1);
        printf("%d: received ping\n", pid);

        char s_send[1] = " ";
        close(pipe_fd1[0]);
        write(pipe_fd1[1], s_send, 1);
        close(pipe_fd1[1]);
        close(pipe_fd0[0]);
        exit(0);
        
    }
    else if(son_proc > 0){
        int pid_p = getpid();
        char send[1] = " ";
        close(pipe_fd0[0]);
        write(pipe_fd0[1], send, 1);

        char p_receive[1];
        close(pipe_fd1[1]);
        read(pipe_fd1[0], p_receive, 1);
        printf("%d: received pong\n", pid_p);
        close(pipe_fd0[1]);
        close(pipe_fd1[0]);
        exit(0);

    }
    else{
        printf("Fork Failed\n");
        exit(-1);
    }

    exit(0);
}

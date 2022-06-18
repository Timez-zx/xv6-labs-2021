#include "kernel/types.h"
#include "user/user.h"
// 每次通道关闭 read才会出现0的情况，其他时候无数据空置
void prime_fork(int pipe_left[2]){
    int prime[1];
    close(pipe_left[1]);
	if(read(pipe_left[0], prime, 4) == 0){
		exit(0);
	}else{
		printf("prime %d\n", prime[0]);
	}

    int pipe_right[2];
    if(pipe(pipe_right) == -1){
        printf("Pipe Failed\n");
        exit(-1);
    }

    int new_num[1];
    int son_proc = fork();
    if(son_proc > 0){
        close(pipe_right[0]);
        while(read(pipe_left[0], new_num, 4)){
            if(new_num[0] % prime[0] != 0){
                write(pipe_right[1], new_num ,4);
            }
        }
        close(pipe_right[1]);
        wait(0);
        exit(0);

    }
    else if(son_proc == 0){
        prime_fork(pipe_right);
    }
    else{
        printf("Fork Failed\n");
        exit(-1);
    }
}

int main(int argc, char *argv[]){
    int initial = 35;
    int pipe_initial[2];
    if(pipe(pipe_initial) == -1){
        printf("Pipe Failed\n");
        exit(-1);
    }
    int son_proc = fork();
    if(son_proc > 0){
        close(pipe_initial[0]);
        for(int i = 2; i <= initial; i++){
            write(pipe_initial[1], &i, 4);
        }
        close(pipe_initial[1]);
    }
    else if(son_proc == 0){
        prime_fork(pipe_initial);
    }
    else{
        printf("Fork Failed\n");
        exit(-1);
    }
    wait(0);
    exit(0);
}
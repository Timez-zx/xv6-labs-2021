#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

//正常输出的通道为stdin是1，stdout是0，对应write和read，对应的一个通道的两端，0是通道的输出，1为输入
char* strsep(char** string){
    int length = strlen(*string);
    int count = 0;
    char* sep;
    while(count < length){
        if((*string)[0] == '\n'){
            sep = (char*) malloc(count+1);
            memcpy(sep, *string - count, count);
            *string = *string + 1;
            return sep;
        }
   
        (*string)++;
        count++;
    }
    sep = (char*) malloc(length+1);
    memcpy(sep, *string - length, length);
    return sep;
}


int main(int argc, char *argv[])
{
    if(argc < 2){
        fprintf(2, "Error:No command input\n");
        exit(1);
    }
    char *command;
    char ** para;
    char *buf;
    char *sep;
    int son_proc;

    buf =  (char*) malloc(128);
    command = (char*) malloc(strlen(argv[1])+1);
    strcpy(command, argv[1]);

    para = (char**) malloc(sizeof(char *)*(MAXARG));
    para[0] = (char*) malloc(strlen(argv[1])+1);
    strcpy(para[0], argv[1]);
    for(int i = 2; i < argc; i++){
        para[i-1] = (char*) malloc(strlen(argv[i])+1);
        strcpy(para[i-1], argv[i]);
    }

    while(read(0, buf + strlen(buf), 128) > 0);
    while(1){
        sep = strsep(&buf);
        if(!strcmp(sep,""))
            break;
        para[argc-1] = (char*) malloc(strlen(sep)+1);
        strcpy(para[argc-1], sep);
        son_proc = fork();
        if(son_proc == 0){
            exec(command, para);
        }
        wait((int*) 0);
    }
    exit(0);
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// grep以及find正则表达对于*判断有bug
void find(char *path, char *filename);

int match(char*, char*);

int matchhere(char*, char*);

int matchstar(int, char*, char*);

int main(int argc, char *argv[])
{
    if(argc != 3){
        printf("Error: find [path] [filename]\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}

void find(char *path, char *filename){

    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
    }

    if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("ls: path too long\n");
        exit(1);
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
            printf("ls: cannot stat %s\n", buf);
            continue;
        }
        if(st.type == T_FILE){

            // if(!strcmp(de.name, filename)){
            //     write(1, buf, strlen(buf));
            //     write(1, "\n", 1);
            //     // printf("%s\n", buf);
            // }

            if(match(filename, de.name)){
                write(1, buf, strlen(buf));
                write(1, "\n", 1);
            }
                
        }
        else if(st.type == T_DIR){
            if((!strcmp(de.name, ".")) | (!strcmp(de.name, ".."))){
                continue;
            }
            else{
                find(buf, filename);
            }
        }
    }
    close(fd);

}




int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*'){
    // printf("%c %c\n", re[0], *text);
    return matchstar(re[0], re+2, text);}
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text)){
    // printf("%c %c\n", re[0], *text);
    return matchhere(re+1, text+1);}
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}
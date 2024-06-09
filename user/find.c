#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char *path)
{
	static char buf[DIRSIZ+1]; 
	char *p; 

	// Find first character after last slash.
	for(p=path+strlen(path); p >= path && *p != '/'; p--) 
	;
	p++;  

	// Return blank-padded name.
	if(strlen(p) >= DIRSIZ)
	return p;
	memmove(buf, p, strlen(p));  
	memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
	return buf;
}	

void find(char *path,char *filename){
	char buf[512], *p;
	int fd;
	struct dirent de;
	struct stat st;
	if((fd = open(path, 0)) < 0){  //打开路径
		fprintf(2, "ls: cannot open %s\n", path);
		return;
	}
	//printf("open success!\n");
	if(fstat(fd, &st) < 0){  //fstat获取文件信息，填充结构体st
    	fprintf(2, "ls: cannot stat %s\n", path);
    	close(fd);
    	return;
	}
	//printf("fstat success!\n");
	switch(st.type){
		case T_FILE:
			if(strcmp(de.name,filename)==0){
				printf("%s%s\n",path,filename);
			}
			break;
		case T_DIR:
			if((strlen(path)+1+DIRSIZ+1)>sizeof(buf)){
				printf("find:path too long\n");
				exit(-1);
			}
			strcpy(buf,path);
			p=buf+strlen(buf);
			*p++='/';
			while(read(fd,&de,sizeof(de))==sizeof(de)){
				//printf("%s\n",de.name);
				if(de.inum==0||(strcmp(de.name,".")==0)||(strcmp(de.name,"..")==0))
					continue;
				memmove(p,de.name,DIRSIZ);
				p[DIRSIZ]=0;
				//printf("%s\n",buf);
				if(stat(buf, &st) < 0){
					printf("find: cannot stat %s\n", buf);
					continue;
				}
				if(st.type==T_FILE){
					if(strcmp(de.name,filename)==0){
						printf("%s\n",buf);
					}	
				}
				else if(st.type==T_DIR){
					find(buf,filename);
				}
			}
			break;
	}
	close(fd);
}

int main(int argc,char *argv[]){
	if(argc!=3){
		printf("find <Dir> <filename>\n");
		exit(-1);
	}
	find(argv[1],argv[2]);
	exit(0);
}



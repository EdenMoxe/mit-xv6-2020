#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
int main(){
	int pipe1[2]={0};
	int pipe2[2]={0};
	char data[1]={'T'};
	if(pipe(pipe1)<0){
		printf("pipe1 failed\n");
		exit(-1);
	}
	if(pipe(pipe2)<0){
		printf("pipe2 failed\n");
		exit(-1);
	}
	pid_t pid = fork();
	if(pid<0){
		printf("fork failed\n");
		exit(-1);
	}
	else if(pid == 0){
		if(read(pipe1[0],data,1)!=1){
			printf("pipe1 recv failed\n");
			exit(-1);
		}
		close(pipe1[0]);
		printf("%d: received ping\n",getpid());
		if(write(pipe2[1],data,1)!=1){
			printf("pipe2 send failed\n");
			exit(-1);                      
		}
		close(pipe2[1]);
		exit(0);
			
	}
	else{
		if(write(pipe1[1],data,1)!=1){
			printf("pipe1 send failed\n");
			exit(-1);
		}
		close(pipe1[1]);
		if(read(pipe2[0],data,1)!=1){
			printf("pipe2 recv failed\n");
			exit(-1);
		}
		close(pipe2[0]);
		printf("%d: received pong\n",getpid());
		wait(NULL);
		exit(0);
	}
}

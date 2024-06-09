#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

#define errlog(error) do{printf(error);exit(-1);}while(0);


void primes(int *fds){
	int primesnum;
	int temp;
	close(fds[1]);
	if(read(fds[0],(void *)&primesnum,4)!=4){
		errlog("read last failed\n");
	}
	printf("prime %d\n",primesnum);
	if(read(fds[0],(void *)&temp,4)==4){
		int new_fds[2];
		if(pipe(new_fds)<0){
			errlog("new pipe failed!\n");
		}
		pid_t pid=fork();
		if(pid<0){
			errlog("fork failed\n");
		}
		else if(pid==0){
			primes(new_fds);
		}	
		else {
			close(new_fds[0]);
			close(fds[1]);
			do{
				if(temp%primesnum!=0){
					if(write(new_fds[1],(void*)&temp,4)!=4){
						errlog("write next failed!\n");
					}	
				}
			}while(read(fds[0],(void*)&temp,4)==4);	
			close(new_fds[1]);
			wait(NULL);
			exit(0);
		}
	}
}

int main(){
	int start = 2;
	int end = 35;
	int pipefd[2];
	if(pipe(pipefd)<0){
		errlog("first pipe failed\n");
	}
	pid_t pid=fork();
	if(pid<0){
		errlog("first fork failed!\n");
	}
	else if(pid==0){
		primes(pipefd);	
	}
	else{
		close(pipefd[0]);
		for(int i=start;i<=end;i++){
			if(write(pipefd[1],(void*)&i,4)!=4){
				errlog("main write failed!\n");
			}
		}
		close(pipefd[1]);
		wait(NULL);
		exit(0);
	}
}

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,const char *argv[]){
	if(argc!=2){
		printf("Please Input ./sleep <tiem>!\n");
		exit(-1);
	}
	else{
		int time = atoi(argv[1]);
		sleep(time);
		exit(0);
	}


}

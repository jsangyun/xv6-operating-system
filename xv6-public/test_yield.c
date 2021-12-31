#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char* argv[])
{
	int pid = fork();

	if(pid < 0){
		printf(1, "Fork Failed\n");
		exit();
	}
	//child process
	if(pid == 0){
		for(int i=0; i<5; i++){
			printf(1, "Child\n");
			yield();
		}
	}
	else{
		for(int i=0; i<5; i++){
			printf(1, "Parent\n");
			yield();
		}
	}
	exit();
}

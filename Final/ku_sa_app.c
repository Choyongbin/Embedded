#include <stdio.h>
#include <unistd.h>

#include "ku_sa_lib.c"

void main(void){
	int cmd =0;
	//printf("asdfasdfd\n");
	scanf("%d",&cmd);
	switch(cmd){
		case 1: 
			ku_sa_snd();
			break;
		case 2:
			ku_sa_rcv();
			break;
		default:
			break;
	}	
}

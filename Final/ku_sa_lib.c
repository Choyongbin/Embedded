#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define IOCTL_START_NUM 0x80
#define IOCTL_NUM1 IOCTL_START_NUM+1
#define IOCTL_NUM2 IOCTL_START_NUM+2

#define KU_IOCTL_NUM 'z'
#define KU_SA_SND _IOWR(KU_IOCTL_NUM, IOCTL_NUM1, unsigned long)
#define KU_SA_RCV _IOWR(KU_IOCTL_NUM, IOCTL_NUM2, unsigned long)

long ku_sa_snd(){
	int fd, ret;
	fd = open("/dev/ku_sa", O_RDWR);
	ret = ioctl(fd, KU_SA_SND, NULL);
	close(fd);
	return ret;
}

long ku_sa_rcv(){
	int fd, ret;
	fd = open("/dev/ku_sa", O_RDWR);
	ret = ioctl(fd, KU_SA_RCV, NULL);
	close(fd);
	return ret;
}	

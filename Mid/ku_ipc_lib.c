#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "ku_ipc.h"

#define IOCTL_START_NUM 0x80
#define KU_IOCTL_NUM 'z'
#define IOCTL_NUM5 IOCTL_START_NUM+5
#define KU_IPC_CLOSE _IOWR(KU_IOCTL_NUM, IOCTL_NUM5, unsigned long)

struct msgbuf
{
	long type;
	char *text;
};

struct msg_form
{
	int msqid;
	struct msgbuf msg;
	int msgsz;
	long msgtyp;
};

int ku_msgget(int key, int msgflg)
{
	int fd, ret; 
	fd = open("/dev/ku_ipc_dev", O_RDWR);
	ret = ioctl(fd, msgflg, key);
	close(fd);
	return ret;
}

int ku_msgclose(int msqid)
{
	int fd, ret;
	fd = open("/dev/ku_ipc_dev", O_RDWR);
	ret = ioctl(fd, KU_IPC_CLOSE, msqid);
	close(fd);
	return ret;

}

long ku_msgsnd(int msqid, void *msgp, int msgsz, int msgflg)
{
	int fd, ret, ret2;
	struct msgbuf *buf = (struct msgbuf *)msgp;
	struct msg_form new_msg = {
		msqid, *buf, msgsz, -1
	};
	fd = open("/dev/ku_ipc_dev", O_RDWR);
	ret = ioctl(fd, msgflg, &new_msg);
	close(fd);
	return ret;
}

long ku_msgrcv(int msqid, void *msgp, int msgsz, long msgtyp, int msgflg)
{
	int fd;
        long ret;
	struct msgbuf *buf = (struct msgbuf *)msgp;
	struct msg_form new_msg = {
		msqid, *buf, msgsz, 1
	};
	fd = open("/dev/ku_ipc_dev", O_RDWR);
	ret = ioctl(fd, msgflg, &new_msg);
	close(fd);
	return ret;
}

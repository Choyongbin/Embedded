#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include "ku_ipc.h"

#define IOCTL_START_NUM 0x80
#define KU_IOCTL_NUM 'z'
#define IOCTL_NUM5 IOCTL_START_NUM+5
#define KU_IPC_CLOSE _IOWR(KU_IOCTL_NUM, IOCTL_NUM5, unsigned long)

MODULE_LICENSE("GPL");

#define DEV_NAME "ku_ipc_dev"

struct msgbuf
{
	long type;
	char *text;
};

struct mybuf
{
	struct msgbuf msg;
	int msgsz;
};

struct msg_form
{
	int msqid;
	struct msgbuf msg;
	int msgsz;
	long msgtyp;
};

struct snd_storage
{
	int msqid;
	int msgsz;
};

struct rcv_storage
{
	int msgsz;
	int msqid;
	long type;
};

struct snd_list
{
	struct list_head list;
	struct snd_storage snd_st;
};

struct rcv_list
{
	struct list_head list;
	struct rcv_storage rcv_st;
};

static struct snd_list snd_queue;
static struct rcv_list rcv_queue;

static int queue_box[10] = {0,0,0,0,0,0,0,0,0,0};
static int queue_size[10] = {0,0,0,0,0,0,0,0,0,0};
static int queue_use[10] = {0,0,0,0,0,0,0,0,0,0};
static int queue_rcvflag[10] = {0,0,0,0,0,0,0,0,0,0};
static int queue_sndflag[10] = {0,0,0,0,0,0,0,0,0,0};

spinlock_t my_lock;

static long my_data[10];

static dev_t dev_num;
static struct cdev *cd_cdev;

struct msgbuf *kern_buf;

wait_queue_head_t my_wq[10];

struct msg_list{
	struct list_head list;
	struct mybuf myb;
};

static struct msg_list msg_queue[10];

static void wakeup_snd(int msqid);

static int ku_create(int key)
{
	if(queue_box[key] >= KU_IPC_MAXMSG)
		return -1;
	else if(queue_size[key] >= KU_IPC_MAXVOL)
		return -1;
	else{
		queue_box[key]++;
		wakeup_snd(key);
		return key;
	}	
}

static int ku_excl(int key)
{
	if(queue_box[key] != 0)
		return -1;
	return 0;
}

static int ku_close(int key)
{
	if(queue_box[key] == 0)
		return -1;
	else if(queue_box[key] - queue_use[key] >0){
		queue_box[key]--;
		return 0;
	}
	else
		return -1;
}


static int wakeup_rcv(struct msg_form *buf)
{
	struct rcv_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	if(!list_empty(&rcv_queue.list)){
		list_for_each_safe(pos, q, &rcv_queue.list){
			tmp = list_entry(pos, struct rcv_list, list);
			if(tmp->rcv_st.type == buf-> msg.type && tmp->rcv_st.msgsz <= buf -> msgsz && buf->msqid == tmp->rcv_st.msqid){
				spin_lock(&my_lock);
				my_data[buf->msqid] = 1;
				spin_unlock(&my_lock);
				list_del(pos);
				kfree(tmp);
				wake_up_interruptible(&my_wq[buf->msqid]);
				return 0;
			}
		}
		return -1;
	}
	return -1;
}

static int ku_snd_nowait(struct msg_form *buf)
{
	struct msg_list *tmp = 0;
	if(queue_use[buf->msqid] >= KU_IPC_MAXMSG)
		return -1;
	else if(queue_size[buf->msqid] + buf->msgsz >= KU_IPC_MAXVOL)
		return -1;
	else if(queue_box[buf->msqid] - queue_use[buf->msqid] <= 0)
		return -1;
	else{
		spin_lock(&my_lock);
		tmp =(struct msg_list *)kmalloc(sizeof(struct msg_list), GFP_KERNEL);
		tmp->myb.msg.type = buf->msg.type;
		tmp->myb.msg.text = buf->msg.text;
		tmp->myb.msgsz = buf->msgsz;
		list_add(&tmp->list, &msg_queue[buf->msqid].list);
		queue_use[buf->msqid]++;
		queue_size[buf->msqid] = queue_size[buf->msqid] + buf->msgsz;
		spin_unlock(&my_lock);

		wakeup_rcv(buf);
	
		return 0;
	}
}

static int ku_snd(struct msg_form *buf)
{
	int ret;
	struct snd_list *tmp = 0;
	if(queue_size[buf->msqid]+buf->msgsz >= KU_IPC_MAXVOL || queue_box[buf->msqid] - queue_use[buf->msqid] == 0){
		spin_lock(&my_lock);
		my_data[buf->msqid] = 0;
		queue_sndflag[buf->msqid]++;
		tmp = (struct snd_list *)kmalloc(sizeof(struct snd_list), GFP_KERNEL);
		tmp -> snd_st.msqid = buf -> msqid;
		tmp -> snd_st.msgsz = buf -> msgsz;
		list_add_tail(&tmp->list, &snd_queue.list);
		spin_unlock(&my_lock);
		ret = wait_event_interruptible_exclusive(my_wq[buf->msqid], my_data[buf->msqid] > 0);
		queue_sndflag[buf->msqid]--;
		if(ret < 0)
			return ret;
	}

	ret = ku_snd_nowait(buf);
	return ret;
}

static int search_msgtyp(long type, int msqid, int msgsz)
{
	struct msg_list *tmp = (struct msg_list*)kmalloc(sizeof(struct msg_list), GFP_KERNEL);
	struct list_head *pos =0;
	unsigned int i = 0;
	if(queue_box[msqid] - queue_use[msqid] < 0)
		return -1;
	
	list_for_each(pos, &msg_queue[msqid].list){
		tmp = list_entry(pos, struct msg_list, list);
		if(tmp->myb.msg.type == type && tmp->myb.msgsz <= msgsz)
			return i;
		i++;
	}
	return -1;
}

static void wakeup_snd(int msqid)
{
	struct snd_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	list_for_each_safe(pos, q, &snd_queue.list){
		tmp = list_entry(pos, struct snd_list, list);
		if(tmp->snd_st.msgsz + queue_size[tmp->snd_st.msqid] <= KU_IPC_MAXVOL && msqid == tmp->snd_st. msqid && queue_box[msqid] - queue_use[msqid] > 0){
			spin_lock(&my_lock);
			my_data[msqid] = 1;
			spin_unlock(&my_lock);
			list_del(pos);
			kfree(tmp);
			wake_up_interruptible(&my_wq[msqid]);
		}

	}
}

static int ku_rcv_nowait(struct msg_form *buf)
{
	struct msg_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	unsigned int i = 0;
	int isExist = search_msgtyp(buf->msgtyp, buf->msqid, buf->msgsz);
	if(isExist == -1)
		return -1;
	list_for_each_safe(pos, q, &msg_queue[buf->msqid].list){
		tmp = list_entry(pos, struct msg_list, list);
		if(i == isExist){
			list_del(pos);
			queue_use[buf->msqid]--;
			kfree(tmp);
			wakeup_snd(buf->msqid);
			return tmp->myb.msgsz;
		}
		i++;
		kfree(tmp);
	}
	return -1;
}

static int ku_rcv(struct msg_form *buf)
{
	int ret=0;
	struct rcv_list *tmp = 0;
	int isExist = search_msgtyp(buf->msg.type, buf->msqid, buf->msgsz);

	if(isExist == -1){
		spin_lock(&my_lock);
		my_data[buf->msqid] = 0;
		queue_rcvflag[buf->msqid]++;
		tmp = (struct rcv_list *)kmalloc(sizeof(struct rcv_list), GFP_KERNEL);
		tmp -> rcv_st.msqid = buf -> msqid;
		tmp -> rcv_st.type = buf -> msgtyp;
		tmp -> rcv_st.msgsz = buf -> msgsz;
		list_add_tail(&tmp -> list , &rcv_queue.list);
		spin_unlock(&my_lock);
		ret = wait_event_interruptible_exclusive(my_wq[buf->msqid], my_data[buf->msqid] > 0);
		if(ret < 0 )
			return -1;
		queue_rcvflag[buf->msqid]--;	
	}
	ret = ku_rcv_nowait(buf);
	return ret;
}

static int ku_rcv_noerror(struct msg_form *buf)
{
	struct msg_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	list_for_each_safe(pos,q,&msg_queue[buf->msqid].list){
		tmp = list_entry(pos, struct msg_list, list);
		if(tmp -> myb.msg.type == buf->msgtyp){
			list_del(pos);
			queue_use[buf->msqid]--;
			kfree(tmp);
			return buf->msgsz;
		}
	}
	return -1;
		
}

static long ku_ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	int msqid = 0;
	struct msg_form *user_buf = (struct msg_form*)vmalloc(sizeof(struct msg_form));
	switch(cmd){
		case KU_IPC_CREATE:
			msqid = (int) arg;
			ret = ku_create(msqid);
			return ret;
		case KU_IPC_EXCL:
			msqid = (int) arg;
			ret = ku_excl(msqid);
			return ret;
		case KU_IPC_CLOSE:
			msqid = (int) arg;
			ret = ku_close(msqid);
			return ret;
		case 0:
			user_buf = (struct msg_form*)arg;
			if(user_buf->msgtyp == -1)
				ret = ku_snd(user_buf);
			else
				ret = ku_rcv(user_buf);
			return ret;

		case KU_IPC_NOWAIT:
			user_buf = (struct msg_form*)arg;
			if(user_buf->msgtyp == -1)
				ret = ku_snd_nowait(user_buf);
			else
				ret = ku_rcv_nowait(user_buf);
			return ret;

		case KU_MSG_NOERROR:
			user_buf = (struct msg_form*)arg;
			ret = ku_rcv_noerror(user_buf);

			return ret;
		default :
			return -1;
	}

}

static int ku_ipc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ku_ipc_release(struct inode *inode, struct file *file)
{
	return 0;
}

struct file_operations ku_ipc_fops = {
	.unlocked_ioctl = ku_ipc_ioctl,
	.open = ku_ipc_open,
	.release = ku_ipc_release
};

static int __init ku_ipc_init(void){
	int ret;
	unsigned int i = 0;
	for(i=0; i<10; i++){
		INIT_LIST_HEAD(&msg_queue[i].list);
	}
	INIT_LIST_HEAD(&snd_queue.list);
	INIT_LIST_HEAD(&rcv_queue.list);
	alloc_chrdev_region(&dev_num, 0,1,DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &ku_ipc_fops);
	ret = cdev_add(cd_cdev, dev_num, 1);
	if(ret <0){
		return -1;
	}

	kern_buf = (struct msgbuf*)vmalloc(sizeof(struct msgbuf));
	memset(kern_buf, '\0', sizeof(struct msgbuf));
	
	spin_lock_init(&my_lock);
	i=0;
	for(i=0; i<10; i++){
		init_waitqueue_head(&my_wq[i]);
	}

	return 0;
}

static void __exit ku_ipc_exit(void){
	struct msg_list *tmp = 0;
	struct snd_list *snd_tmp = 0;
	struct rcv_list *rcv_tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	unsigned int i = 0;
	
	for(i=0; i<10; i++){
		list_for_each_safe(pos,q,&msg_queue[i].list){
			tmp = list_entry(pos, struct msg_list, list);
			list_del(pos);
			kfree(tmp);
		}
	}

	pos = 0;
	q = 0;
	list_for_each_safe(pos, q, &snd_queue.list){
		snd_tmp = list_entry(pos,struct snd_list, list);
		list_del(pos);
		kfree(snd_tmp);
	}

	pos = 0;
	q = 0;
	list_for_each_safe(pos,q, &rcv_queue.list){
		rcv_tmp = list_entry(pos,struct rcv_list, list);
		list_del(pos);
		kfree(rcv_tmp);
	}
	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);
}

module_init(ku_ipc_init);
module_exit(ku_ipc_exit);


#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <asm/delay.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define ULTRA_TRIG 17
#define ULTRA_ECHO 18

#define PIN1 6
#define PIN2 13
#define PIN3 19
#define PIN4 26

#define STEPS 8
#define ONEROUND 128

#define IOCTL_START_NUM 0x80
#define IOCTL_NUM1 IOCTL_START_NUM+1
#define IOCTL_NUM2 IOCTL_START_NUM+2

#define KU_IOCTL_NUM 'z'
#define KU_SA_SND _IOWR(KU_IOCTL_NUM, IOCTL_NUM1, unsigned long)
#define KU_SA_RCV _IOWR(KU_IOCTL_NUM, IOCTL_NUM2, unsigned long)

#define DEV_NAME "ku_sa"


static int irq_num;
static int echo_valid_flag = 3;

static ktime_t echo_start;
static ktime_t echo_stop;
static int flag = 0;

struct my_timer_info{
	struct timer_list timer;
	long delay_jiffies;
	int data;
};

struct sensor_list{
	struct list_head list;
	int cm;
};

int blue[8] = {1,1,0,0,0,0,0,1};
int pink[8] = {0,1,1,1,0,0,0,0};
int yellow[8] = {0,0,0,1,1,1,0,0};
int orange[8] = {0,0,0,0,0,1,1,1};

spinlock_t my_lock;
static struct my_timer_info my_timer;
static struct sensor_list mylist;

static irqreturn_t simple_ultra_isr(int irq, void* dev_id){
	ktime_t tmp_time;
	s64 time;
	int cm;
	struct sensor_list *tmp = 0;
	tmp_time = ktime_get();
	if(echo_valid_flag == 1){
		//printk("111111111111\n");
		if(gpio_get_value(ULTRA_ECHO) == 1){
			echo_start = tmp_time;
			echo_valid_flag =2;
		}		
	}
	else if(echo_valid_flag == 2){
		//printk("222222222222222\n");
		if(gpio_get_value(ULTRA_ECHO)==0){
			echo_stop = tmp_time;
			time = ktime_to_us(ktime_sub(echo_stop, echo_start));
			cm = (int) time / 58;
			tmp = (struct sensor_list*)kmalloc(sizeof(struct sensor_list), GFP_KERNEL);
			tmp->cm = cm;
			list_add_tail(&tmp->list, &mylist.list);
			printk("simple_ultra : detect %d cm\n", cm);
			udelay(500);
			echo_valid_flag = 3;
		}
	}
	return IRQ_HANDLED;	
}


void ultra_init(void){
	int ret;
	gpio_request_one(ULTRA_TRIG, GPIOF_OUT_INIT_LOW, "ULTRA_TRIG");
	gpio_request_one(ULTRA_ECHO, GPIOF_IN, "ULTRA_ECHO");
	irq_num = gpio_to_irq(ULTRA_ECHO);

	ret = request_irq(irq_num, simple_ultra_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "ULTRA_ECHO", NULL);

	if(ret){
		//printk("abdsjsfjksljfls    %d \n", ret);
		free_irq(irq_num, NULL);
	}
	else{
		if(echo_valid_flag == 3){
			//printk("333333333333333333\n");
			echo_start = ktime_set(0,1);
			echo_stop = ktime_set(0,1);
			echo_valid_flag = 0;

			gpio_set_value(ULTRA_TRIG,1);
			udelay(10);
			gpio_set_value(ULTRA_TRIG,0);

			echo_valid_flag = 1;
			
		}
	}
}

void setstep(int p1, int p2, int p3, int p4){
	gpio_set_value(PIN1, p1);
	gpio_set_value(PIN2, p2);
	gpio_set_value(PIN3, p3);
	gpio_set_value(PIN4, p4);
}

void forward(int round, int delay){
	int i=0, j=0;
	for(i=0; i<ONEROUND * round; i++){
		for(j=0; j<STEPS; j++){
			setstep(blue[j], pink[j], yellow[j], orange[j]);
			udelay(delay);
		}
	}
	setstep(0,0,0,0);
}

void backward(int round, int delay){
	int i=0, j=0;
	for(i=0; i<ONEROUND * round; i++){
		for(j=STEPS; j>0; j--){
			setstep(blue[j], pink[j], yellow[j], orange[j]);
			udelay(delay);
		}
	}
	setstep(0,0,0,0);
}

static void start_motor(void){
	struct sensor_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	unsigned int i = 0;
	unsigned int sum = 0;
	list_for_each_safe(pos, q, &mylist.list){
		tmp = list_entry(pos, struct sensor_list, list);
		sum += tmp -> cm;
		list_del(pos);
		kfree(tmp);
		i++;
	}
	printk("%d\n", (int)sum/i);
	if((int)sum/i>15){
		forward(1,1500);
	}
	else if((int)sum/i< 15 && (int)sum/i>7){
		printk("stop\n");
	}
	else{
		backward(1,1500);
	}
}

static void my_timer_func(struct timer_list *t){
	struct my_timer_info *info = from_timer(info, t, timer);
	
	info->data++;
	printk("%d\n",info->data);
	if(info->data % 3 == 0){
		start_motor();
		mod_timer(&my_timer.timer, jiffies + info->delay_jiffies);
	}
	else{
		ultra_init();
		mod_timer(&my_timer.timer, jiffies + info->delay_jiffies);
	}
	//udelay(500);

}

static void timer_init(void){
	printk("simple_timer : init modules \n");

	my_timer.delay_jiffies = msecs_to_jiffies(100);
	my_timer.data = 100;
	timer_setup(&my_timer.timer, my_timer_func, 0);
	my_timer.timer.expires = jiffies + my_timer.delay_jiffies;
	add_timer(&my_timer.timer);
}

static long ku_sa_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	switch(cmd){
		case KU_SA_SND:
			timer_init();
			break;
		case KU_SA_RCV:
			del_timer(&my_timer.timer);
			break;
		default:
			break;
	}
	return 0;
}

static int ku_sa_open(struct inode *inode, struct file *file){
	return 0;
}

static int ku_sa_release(struct inode *inode, struct file *file){
	return 0;
}

struct file_operations ku_sa_fops={
	.unlocked_ioctl = ku_sa_ioctl,
	.open = ku_sa_open,
	.release = ku_sa_release
};

static dev_t dev_num;
static struct cdev *cd_cdev;

static int __init ku_sa_init(void){
	int ret = 0;
	INIT_LIST_HEAD(&mylist.list);

	gpio_request_one(PIN1, GPIOF_OUT_INIT_LOW, "p1");
	gpio_request_one(PIN2, GPIOF_OUT_INIT_LOW, "p2");
	gpio_request_one(PIN3, GPIOF_OUT_INIT_LOW, "p3");
	gpio_request_one(PIN4, GPIOF_OUT_INIT_LOW, "p4");

	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &ku_sa_fops);
	ret = cdev_add(cd_cdev, dev_num, 1);

	return 0;
}

static void __exit ku_sa_exit(void){
	struct sensor_list *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	unsigned int i = 0;

	list_for_each_safe(pos,q,&mylist.list){
		tmp = list_entry(pos, struct sensor_list, list);
		list_del(pos);
		kfree(tmp);
		i++;
	}
	gpio_free(PIN1);
	gpio_free(PIN2);
	gpio_free(PIN3);
	gpio_free(PIN4);
	
	free_irq(irq_num, NULL);

	del_timer(&my_timer.timer);

	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);

}

module_init(ku_sa_init);
module_exit(ku_sa_exit);

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include "cbuffer.h"


#define MAX_ITEMS_CBUFFER	50
#define MAX_CHARS_KBUF		50

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Implementation of a FIFO using a /proc entry");
MODULE_AUTHOR("Carlos Martinez & Sergio Pino");

static struct proc_dir_entry *proc_entry;
static cbuffer_t* cbuffer;
int prod_count = 0; /* Number of processes who opened /proc entry for writing (producers) */
int cons_count = 0; /* Number of processes who opened /proc entry for reading (consumers) */
struct semaphore mtx; /* This guarantees mutual exclusion */
struct semaphore sem_prod; /* Queue for producers */
struct semaphore sem_cons; /* Queue for consumers */
int nr_prod_waiting = 0; /* Number of producers waiting */
int nr_cons_waiting = 0; /* Number of consumers waiting */



static int fifoproc_open(struct inode *inode, struct file *file) {	
	if (down_interruptible(&mtx)) {
		return -EINTR;
	}
	
	if (file->f_mode & FMODE_READ) {
		/* A consumer opens FIFO */
		cons_count++;

		while (nr_prod_waiting) {
			nr_prod_waiting--;
			up(&sem_prod);
		}
		
		while (!prod_count) {
			nr_cons_waiting++;
			up(&mtx);
			if (down_interruptible(&sem_cons)) { 
				down(&mtx);
				nr_cons_waiting--;
				up(&mtx);
				down(&mtx);
				cons_count--;
				up(&mtx);
				return -EINTR;
			}
			if (down_interruptible(&mtx)) {
				down(&mtx);
				cons_count--;
				up(&mtx);
			}
		}
	} else {
		/* A producer opens FIFO */
		prod_count++;
		
		while (nr_cons_waiting) {
			nr_cons_waiting--;
			up(&sem_cons);
		}
		
		while (!cons_count) {
			nr_prod_waiting++;
			up(&mtx);
			if (down_interruptible(&sem_prod)) { 
				down(&mtx);
				nr_prod_waiting--;
				up(&mtx);
				down(&mtx);
				prod_count--;
				up(&mtx);
				return -EINTR;
			}
			if (down_interruptible(&mtx)) {
				down(&mtx);
				prod_count--;
				up(&mtx);
			}
		}
	}
	
	up(&mtx);
	
	return 0;
}


static int fifoproc_release(struct inode *inode, struct file *file) {
	if (down_interruptible(&mtx)) {
		return -EINTR;
	}
	
	if (file->f_mode & FMODE_READ) {
		cons_count--;
		
		if (cons_count == 0) {
			while (nr_prod_waiting) {
				nr_prod_waiting--;
				up(&sem_prod);
			}
		}
	} else {
		prod_count--;
	}
	
	if (!(cons_count || prod_count)) {
		clear_cbuffer_t(cbuffer);
	}
	
	up(&mtx);
	
	return 0;
}


static ssize_t fifoproc_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
	char kbuf[MAX_CHARS_KBUF];
	
	if((*off) > 0) { return 0; }
	if(len > MAX_CHARS_KBUF) { return -ENOSPC; }
	
	/* Block mutex */
	if(down_interruptible(&mtx)) {
		return -EINTR;
	}
	
	/* If there isn't producers and pipe is empty, FIFO must be closed */
	if (prod_count == 0 && is_empty_cbuffer_t(cbuffer)) {
		up(&mtx);
		return 0;
	}
	
	/* Block while buffer is empty */
	while (size_cbuffer_t(cbuffer) < len) {
		/* Increments consumers waiting */
		nr_cons_waiting++;
		
		/* Releases the mutex before blocking it */
		up(&mtx);
		
		/* Blocking queue */
		if (down_interruptible(&sem_cons)) {
			down(&mtx);
			nr_cons_waiting--;
			up(&mtx);
			return -EINTR;
		}
		
		if (prod_count == 0 && is_empty_cbuffer_t(cbuffer)) {
			up(&mtx);
			return 0;
		}
		
		/* Acquires the mutex before entering the critical section */
		if (down_interruptible(&mtx)) {
			return -EINTR;
		}
	}
	
	/* Obtains cbuffer data */
	remove_items_cbuffer_t(cbuffer, kbuf, len);
	
	/* Wake up to potential producers */
	while (nr_prod_waiting) {
		up(&sem_prod);
		nr_prod_waiting--;
	}
	
	/* Unblock mutex */
	up(&mtx);
	
	len -= copy_to_user(buf, kbuf, len);
	
	return len;
}


static ssize_t fifoproc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
	char kbuf[MAX_CHARS_KBUF];
	
	if((*off) > 0) { return 0; }
	if(len > MAX_CHARS_KBUF) { return -ENOSPC; }
	
	len -= copy_from_user(kbuf, buf, len);
	
	/* Block mutex */
	if (down_interruptible(&mtx)) { return -EINTR; }
	
	/* If there isn't consumers, FIFO must be closed */
	if (cons_count == 0) {
		up(&mtx);
		return -EPIPE;
	}
	
	/* Producer is blocked if no space */
	while (nr_gaps_cbuffer_t(cbuffer) < len && cons_count > 0) {
		/* Increase producers waiting */
		nr_prod_waiting++;
		
		/* Releases the mutex before blocking it */
		up(&mtx);
		
		/* Blocking queue */
		if (down_interruptible(&sem_prod)) {
			down(&mtx);
			nr_prod_waiting--;
			up(&mtx);
			return -EINTR;
		}
		
		if (cons_count == 0) {
			up(&mtx);
			return -EPIPE;
		}
		
		/* Acquires the mutex before entering the critical section */
		if (down_interruptible(&mtx)) {
			return -EINTR;
		}
	}
	
	/* Insert data received in cbuffer */
	insert_items_cbuffer_t(cbuffer, kbuf, len);
	
	/* Wake up to potential consumers */
	while (nr_cons_waiting) {
		up(&sem_cons);
		nr_cons_waiting--;
	}
	
	/* Unlock mutex */
	up(&mtx);
	
	return len;
}

/*
 * Available operations module
 */
static const struct file_operations proc_entry_fops = {
	.open = fifoproc_open,
	.release = fifoproc_release,
    .read = fifoproc_read,
    .write = fifoproc_write,    
};

/*
 * Module initialization.
 */
int init_fifoproc_module(void) {
	cbuffer = create_cbuffer_t(MAX_ITEMS_CBUFFER);
	
	if(!cbuffer) {
		return -ENOMEM;
	}
	
	sema_init(&sem_prod, 0);
	sema_init(&sem_cons, 0);
	sema_init(&mtx, 1);
	
	proc_entry = proc_create_data("fifoproc", 0666, NULL, &proc_entry_fops, NULL);
	
	if(proc_entry == NULL) {
		destroy_cbuffer_t(cbuffer);
		printk(KERN_INFO "Fifoproc: Couldn't create /proc entry.\n");
		
		return -ENOMEM;
	}
	
	printk(KERN_INFO "Fifoproc: Module loaded.\n");
	
	return 0;
}

/*
 * Unload module
 */
void exit_fifoproc_module(void) {
	remove_proc_entry("fifoproc", NULL);
	destroy_cbuffer_t(cbuffer);
	printk(KERN_INFO "Fifoproc: Module unloaded.\n");
}


module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );

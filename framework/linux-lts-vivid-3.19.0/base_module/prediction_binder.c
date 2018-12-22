/*
 * prediction_binder.c
 */
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#include "base_module.h"

static long prediction_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	/*struct binder_proc *proc = filp->private_data;*/
	/*struct binder_thread *thread;*/
	unsigned int size = _IOC_SIZE(cmd);
	void __user *ubuf = (void __user *)arg;
    struct prediction_parv_t prediction_parv;
    struct timespec ts_stime;

	/*pr_info("prediction_ioctl: %d:%d %x %lx\n",
			proc->pid, current->pid, cmd, arg);*/

    // copy an argument from user-level
    if (size != sizeof(struct prediction_parv_t)) {
        ret = -EINVAL;
        goto out;
    }
    if (copy_from_user(&prediction_parv, ubuf, sizeof(prediction_parv))) {
        ret = -EFAULT;
        goto out;
    }

    // setup parameters (stime)
    getrawmonotonic(&ts_stime);
    prediction_parv.stime = timespec_to_ns(&ts_stime);

    // do prediction
    /*do_prediction(&prediction_parv);*/

    // copy results to user
    if (copy_to_user(ubuf, &prediction_parv, sizeof(prediction_parv))) {
        ret = -EFAULT;
        goto out;
    }

out:
    return ret;
}

/*static int prediction_binder_open(struct inode *nodp, struct file *filp)*/
/*{*/
    /*return 0;*/
/*}*/

/*static int binder_release(struct inode *nodp, struct file *filp)*/
/*{*/
	/*return 0;*/
/*}*/

static const struct file_operations prediction_binder_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = prediction_ioctl,
	.compat_ioctl = prediction_ioctl,
	/*.open = prediction_binder_open,*/
	/*.release = binder_release,*/
};

static struct miscdevice binder_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "prediction_binder",
	.fops = &prediction_binder_fops
};

static int __init prediction_binder_init(void)
{
	int ret;

	ret = misc_register(&binder_miscdev);

	return ret;
}
module_init(prediction_binder_init)


static void __exit prediction_binder_exit(void)
{
    misc_deregister(&binder_miscdev);
}
module_exit(prediction_binder_exit)

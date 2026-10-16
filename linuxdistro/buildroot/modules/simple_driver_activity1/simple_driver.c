/**
 * @brief   An introductory character driver. This module maps to /dev/simple_driver and
 * comes with a helper C program that can be run in Linux user space to communicate with
 * this the LKM.
 *
 * Modified from Derek Molloy (http://www.derekmolloy.ie/)
 */

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define  DEVICE_NAME "simple_driver" ///< The device will appear at /dev/simple_driver using this value
#define  CLASS_NAME  "simple_class"        ///< The device class -- this is a character device driver

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Author Name");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A generic Linux char driver.");  ///< The description -- see modinfo
MODULE_VERSION("0.2");            ///< A version number to inform users

static int    majorNumber;                  ///< Stores the device number -- determined automatically
struct message_node {
    struct list_head list;
    char message[256];
    size_t size;
};

static LIST_HEAD(message_list);
static DEFINE_MUTEX(message_lock);
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class *charClass  = NULL; ///< The device-driver class struct pointer
static struct device *charDevice = NULL; ///< The device-driver device struct pointer

// The prototype functions for the character driver -- must come before the struct definition
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static struct file_operations fops =
{
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};


static int __init simple_init(void){
	printk(KERN_INFO "Simple Driver: Initializing the LKM\n");

	// Try to dynamically allocate a major number for the device -- more difficult but worth it
	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber<0){
		printk(KERN_ALERT "Simple Driver failed to register a major number\n");
		return majorNumber;
	}
	
	printk(KERN_INFO "Simple Driver: registered correctly with major number %d\n", majorNumber);

	// Register the device class
	charClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(charClass)){                // Check for error and clean up if there is
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Simple Driver: failed to register device class\n");
		return PTR_ERR(charClass);          // Correct way to return an error on a pointer
	}
	
	printk(KERN_INFO "Simple Driver: device class registered correctly\n");

	// Register the device driver
	charDevice = device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(charDevice)){               // Clean up if there is an error
		class_destroy(charClass);           // Repeated code but the alternative is goto statements
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Simple Driver: failed to create the device\n");
		return PTR_ERR(charDevice);
	}
	
	printk(KERN_INFO "Simple Driver: device class created correctly\n"); // Made it! device was initialized
		
	return 0;
}

static void __exit simple_exit(void)
{
    struct message_node *node;
    struct message_node *tmp;

    mutex_lock(&message_lock);

    list_for_each_entry_safe(node, tmp, &message_list, list) {
        list_del(&node->list);
        kfree(node);
    }

    mutex_unlock(&message_lock);

    device_destroy(charClass, MKDEV(majorNumber, 0));
    class_unregister(charClass);
    class_destroy(charClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    printk(KERN_INFO "Simple Driver: goodbye from the LKM!\n");
}


static int dev_open(struct inode *inodep, struct file *filep){
	numberOpens++;
	printk(KERN_INFO "Simple Driver: device has been opened %d time(s)\n", numberOpens);
	return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer,
                        size_t len, loff_t *offset)
{
    struct message_node *node;
    size_t message_size;
    int error_count;

    mutex_lock(&message_lock);

    if (list_empty(&message_list)) {
        mutex_unlock(&message_lock);
        return 0;
    }

    node = list_first_entry(&message_list,
                            struct message_node,
                            list);

    message_size = node->size;

    if (len < message_size) {
        mutex_unlock(&message_lock);
        return -EINVAL;
    }

    error_count = copy_to_user(buffer, node->message, message_size);

    if (error_count != 0) {
        printk(KERN_INFO
               "Simple Driver: failed to send message to user\n");
        mutex_unlock(&message_lock);
        return -EFAULT;
    }

    list_del(&node->list);

    mutex_unlock(&message_lock);

    printk(KERN_INFO
           "Simple Driver: sent %zu characters to the user\n",
           message_size);

    kfree(node);

    return message_size;
}


/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *filep, const char *buffer,
                         size_t len, loff_t *offset)
{
    struct message_node *node;

    if (len == 0 || len >= sizeof(node->message)) {
        printk(KERN_INFO
               "Simple Driver: message too long or empty\n");
        return -EINVAL;
    }

    node = kmalloc(sizeof(*node), GFP_KERNEL);
    if (!node) {
        printk(KERN_INFO
               "Simple Driver: failed to allocate message\n");
        return -ENOMEM;
    }

    if (copy_from_user(node->message, buffer, len) != 0) {
        kfree(node);
        return -EFAULT;
    }

    node->message[len] = '\0';
    node->size = len + 1;

    mutex_lock(&message_lock);
    list_add_tail(&node->list, &message_list);
    mutex_unlock(&message_lock);

    printk(KERN_INFO
           "Simple Driver: received %zu characters from the user\n",
           len);

    return len;
}


/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep){
	printk(KERN_INFO "Simple Driver: device successfully closed\n");
	return 0;
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(simple_init);
module_exit(simple_exit);

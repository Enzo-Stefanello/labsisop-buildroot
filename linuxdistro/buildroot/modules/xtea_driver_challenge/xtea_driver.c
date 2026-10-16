#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>

#define DEVICE_NAME "xtea_driver"
#define CLASS_NAME  "xtea_class"

#define MAX_DATA_SIZE 256
#define MAX_RESULT_SIZE (MAX_DATA_SIZE * 2 + 1)

static int majorNumber;
static struct class *xteaClass = NULL;
static struct device *xteaDevice = NULL;
static char *key0 = "f0e1d2c3";
static char *key1 = "b4a59687";
static char *key2 = "78695a4b";
static char *key3 = "3c2d1e0f";

module_param(key0, charp, 0000);
module_param(key1, charp, 0000);
module_param(key2, charp, 0000);
module_param(key3, charp, 0000);

MODULE_PARM_DESC(key0, "XTEA key word 0");
MODULE_PARM_DESC(key1, "XTEA key word 1");
MODULE_PARM_DESC(key2, "XTEA key word 2");
MODULE_PARM_DESC(key3, "XTEA key word 3");

static DEFINE_MUTEX(xtea_lock);

static u32 xtea_key[4];

static char result[MAX_RESULT_SIZE];
static size_t result_size;

/*
 * XTEA encryption.
 * Based on the reference algorithm provided in Tutorial 2.3.
 */
static void xtea_encipher(u32 num_rounds, u32 v[2], const u32 key[4])
{
    u32 i;
    u32 v0 = v[0];
    u32 v1 = v[1];
    u32 sum = 0;
    u32 delta = 0x9E3779B9;

    for (i = 0; i < num_rounds; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1)
              ^ (sum + key[sum & 3]);

        sum += delta;

        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0)
              ^ (sum + key[(sum >> 11) & 3]);
    }

    v[0] = v0;
    v[1] = v1;
}

/*
 * XTEA decryption.
 * Based on the reference algorithm provided in Tutorial 2.3.
 */
static void xtea_decipher(u32 num_rounds, u32 v[2], const u32 key[4])
{
    u32 i;
    u32 v0 = v[0];
    u32 v1 = v[1];
    u32 delta = 0x9E3779B9;
    u32 sum = delta * num_rounds;

    for (i = 0; i < num_rounds; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0)
              ^ (sum + key[(sum >> 11) & 3]);

        sum -= delta;

        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1)
              ^ (sum + key[sum & 3]);
    }

    v[0] = v0;
    v[1] = v1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

static int parse_hex_data(const char *hex, u8 *data, size_t size)
{
    size_t i;
    int high;
    int low;

    for (i = 0; i < size; i++) {
        high = hex_value(hex[i * 2]);
        low = hex_value(hex[i * 2 + 1]);

        if (high < 0 || low < 0)
            return -EINVAL;

        data[i] = (high << 4) | low;
    }

    return 0;
}

static void bytes_to_hex(const u8 *data, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < size; i++) {
        result[i * 2]     = hex[(data[i] >> 4) & 0xf];
        result[i * 2 + 1] = hex[data[i] & 0xf];
    }

    result[size * 2] = '\0';
    result_size = size * 2;
}

static int dev_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "XTEA Driver: device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "XTEA Driver: device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filep,
                        char *buffer,
                        size_t len,
                        loff_t *offset)
{
    size_t size_to_copy;

    mutex_lock(&xtea_lock);

    if (result_size == 0) {
        mutex_unlock(&xtea_lock);
        return 0;
    }

    if (len < result_size) {
        mutex_unlock(&xtea_lock);
        return -EINVAL;
    }

    size_to_copy = result_size;

    if (copy_to_user(buffer, result, size_to_copy) != 0) {
        mutex_unlock(&xtea_lock);
        return -EFAULT;
    }

    result_size = 0;

    mutex_unlock(&xtea_lock);

    printk(KERN_INFO
           "XTEA Driver: returned %zu characters\n",
           size_to_copy);

    return size_to_copy;
}

static ssize_t dev_write(struct file *filep,
                         const char *buffer,
                         size_t len,
                         loff_t *offset)
{
    char input[1024];
    char command[8];
    char size_str[16];
    char data_hex[513];

    u32 data_size;
    u8 data[MAX_DATA_SIZE];

    size_t expected_hex_size;
    size_t i;
    int parsed;
    int ret;

    if (len == 0 || len >= sizeof(input))
        return -EINVAL;

    if (copy_from_user(input, buffer, len) != 0)
        return -EFAULT;

    input[len] = '\0';

    /*
     * Challenge format:
     *
     * enc <data_size> <data>
     * dec <data_size> <data>
     */
    parsed = sscanf(input,
                    "%7s %15s %512s",
                    command,
                    size_str,
                    data_hex);

    if (parsed != 3) {
        printk(KERN_INFO
               "XTEA Driver: invalid command format\n");
        return -EINVAL;
    }

    if (strcmp(command, "enc") != 0 &&
        strcmp(command, "dec") != 0) {
        printk(KERN_INFO
               "XTEA Driver: command must be enc or dec\n");
        return -EINVAL;
    }

    ret = kstrtou32(size_str, 10, &data_size);
    if (ret)
        return -EINVAL;

    if (data_size == 0 ||
        data_size > MAX_DATA_SIZE ||
        (data_size % 8) != 0) {
        printk(KERN_INFO
               "XTEA Driver: data size must be a multiple of 8\n");
        return -EINVAL;
    }

    expected_hex_size = data_size * 2;

    if (strlen(data_hex) != expected_hex_size) {
        printk(KERN_INFO
               "XTEA Driver: invalid hexadecimal data size\n");
        return -EINVAL;
    }

    ret = parse_hex_data(data_hex, data, data_size);
    if (ret)
        return ret;

    /*
     * XTEA works on 64-bit blocks.
     * The key is the one configured through module parameters.
     */
    for (i = 0; i < data_size; i += 8) {
        u32 v[2];

        v[0] = ((u32)data[i] << 24) |
               ((u32)data[i + 1] << 16) |
               ((u32)data[i + 2] << 8) |
               ((u32)data[i + 3]);

        v[1] = ((u32)data[i + 4] << 24) |
               ((u32)data[i + 5] << 16) |
               ((u32)data[i + 6] << 8) |
               ((u32)data[i + 7]);

        if (strcmp(command, "enc") == 0)
            xtea_encipher(32, v, xtea_key);
        else
            xtea_decipher(32, v, xtea_key);

        data[i]     = (v[0] >> 24) & 0xff;
        data[i + 1] = (v[0] >> 16) & 0xff;
        data[i + 2] = (v[0] >> 8) & 0xff;
        data[i + 3] = v[0] & 0xff;

        data[i + 4] = (v[1] >> 24) & 0xff;
        data[i + 5] = (v[1] >> 16) & 0xff;
        data[i + 6] = (v[1] >> 8) & 0xff;
        data[i + 7] = v[1] & 0xff;
    }

    mutex_lock(&xtea_lock);

    bytes_to_hex(data, data_size);

    mutex_unlock(&xtea_lock);

    printk(KERN_INFO
           "XTEA Driver: %s operation completed for %u bytes\n",
           command, data_size);

    return len;
}

static struct file_operations fops =
{
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init xtea_init(void)
{
    int ret;

    printk(KERN_INFO "XTEA Driver: Initializing the LKM\n");

    ret = kstrtou32(key0, 16, &xtea_key[0]);
    if (ret)
        return -EINVAL;

    ret = kstrtou32(key1, 16, &xtea_key[1]);
    if (ret)
        return -EINVAL;

    ret = kstrtou32(key2, 16, &xtea_key[2]);
    if (ret)
        return -EINVAL;

    ret = kstrtou32(key3, 16, &xtea_key[3]);
    if (ret)
        return -EINVAL;

    printk(KERN_INFO
           "XTEA Driver: cryptographic key configured\n");

    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);

    if (majorNumber < 0) {
        printk(KERN_ALERT
               "XTEA Driver: failed to register major number\n");
        return majorNumber;
    }

    xteaClass = class_create(THIS_MODULE, CLASS_NAME);

    if (IS_ERR(xteaClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT
               "XTEA Driver: failed to register device class\n");
        return PTR_ERR(xteaClass);
    }

    xteaDevice = device_create(xteaClass,
                               NULL,
                               MKDEV(majorNumber, 0),
                               NULL,
                               DEVICE_NAME);

    if (IS_ERR(xteaDevice)) {
        class_destroy(xteaClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);

        printk(KERN_ALERT
               "XTEA Driver: failed to create device\n");

        return PTR_ERR(xteaDevice);
    }

    result_size = 0;

    printk(KERN_INFO
           "XTEA Driver: registered correctly with major number %d\n",
           majorNumber);

    return 0;
}

static void __exit xtea_exit(void)
{
    device_destroy(xteaClass, MKDEV(majorNumber, 0));
    class_unregister(xteaClass);
    class_destroy(xteaClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    printk(KERN_INFO "XTEA Driver: goodbye from the LKM!\n");
}

module_init(xtea_init);
module_exit(xtea_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("PUCRS");
MODULE_DESCRIPTION("XTEA character device driver");
MODULE_VERSION("1.0");

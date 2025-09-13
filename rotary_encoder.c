/*
 * Raspberry Pi Rotary Encoder Device Driver
 * 
 * This driver supports up to 4 rotary encoders with the following features:
 * - Configurable ranges per encoder
 * - Button press detection
 * - Position and direction tracking
 * - Non-blocking read operations
 *
 * Author: Mark Mackelprang
 * License: MIT
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>

#define DEVICE_NAME "rotary_encoder"
#define CLASS_NAME "rotary_encoder"
#define MAX_ENCODERS 4

/* IOCTL commands */
#define ROTARY_IOC_MAGIC 'R'
#define ROTARY_SET_RANGE    _IOW(ROTARY_IOC_MAGIC, 1, struct rotary_range)
#define ROTARY_GET_POSITION _IOR(ROTARY_IOC_MAGIC, 2, struct rotary_status)
#define ROTARY_RESET        _IOW(ROTARY_IOC_MAGIC, 3, int)
#define ROTARY_SET_DEBOUNCE _IOW(ROTARY_IOC_MAGIC, 4, int)

/* Data structures */
struct rotary_range {
    int encoder_id;
    int min_value;
    int max_value;
};

struct rotary_status {
    int encoder_id;
    int position;
    int direction;  /* -1 for CCW, 1 for CW, 0 for no change */
    int button_pressed;
    unsigned long timestamp;
};

struct encoder_config {
    char name[32];
    int clk_pin;
    int dt_pin;
    int sw_pin;
    int min_range;
    int max_range;
    int enabled;
};

struct rotary_encoder {
    char name[32];
    int clk_gpio;
    int dt_gpio;
    int sw_gpio;
    int position;
    int min_value;
    int max_value;
    int last_clk;
    int last_dt;
    int direction;
    int button_pressed;
    int button_last_state;
    unsigned long last_interrupt_time;
    unsigned long button_last_time;
    unsigned long debounce_time;
    wait_queue_head_t wait_queue;
    int data_ready;
    struct mutex lock;
};

/* Module parameters for GPIO pins */
static int clk_pins[MAX_ENCODERS] = {18, 22, 24, 26};
static int dt_pins[MAX_ENCODERS] = {19, 23, 25, 27};
static int sw_pins[MAX_ENCODERS] = {20, -1, -1, -1}; /* -1 means no button */

static char *config_file = "/etc/rotary_encoder.conf";
module_param(config_file, charp, S_IRUGO);
MODULE_PARM_DESC(config_file, "Configuration file path (default: /etc/rotary_encoder.conf)");

module_param_array(clk_pins, int, NULL, S_IRUGO);
module_param_array(dt_pins, int, NULL, S_IRUGO);
module_param_array(sw_pins, int, NULL, S_IRUGO);

MODULE_PARM_DESC(clk_pins, "GPIO pins for CLK signals (default: 18,22,24,26)");
MODULE_PARM_DESC(dt_pins, "GPIO pins for DT signals (default: 19,23,25,27)");
MODULE_PARM_DESC(sw_pins, "GPIO pins for switch signals (default: 20,-1,-1,-1, -1=no button)");

/* Configuration storage */
static struct encoder_config encoder_configs[MAX_ENCODERS];
static int config_loaded = 0;

/* Global variables */
static dev_t dev_number;
static struct class *rotary_class = NULL;
static struct cdev rotary_cdev;
static struct rotary_encoder encoders[MAX_ENCODERS];
static int num_encoders = 1; /* Default to 1 encoder */

module_param(num_encoders, int, S_IRUGO);
MODULE_PARM_DESC(num_encoders, "Number of encoders to initialize (1-4, default: 1)");

/* Function prototypes */
static irqreturn_t rotary_clk_interrupt(int irq, void *dev_id);
static irqreturn_t rotary_sw_interrupt(int irq, void *dev_id);
static int load_encoder_config(void);
static void init_default_config(void);
static int parse_config_line(char *line);

/* Initialize default configuration */
static void init_default_config(void)
{
    int i;
    
    for (i = 0; i < MAX_ENCODERS; i++) {
        snprintf(encoder_configs[i].name, sizeof(encoder_configs[i].name), "encoder_%d", i);
        encoder_configs[i].clk_pin = clk_pins[i];
        encoder_configs[i].dt_pin = dt_pins[i];
        encoder_configs[i].sw_pin = sw_pins[i];
        encoder_configs[i].min_range = -2147483648; /* INT_MIN */
        encoder_configs[i].max_range = 2147483647;  /* INT_MAX */
        encoder_configs[i].enabled = (i == 0) ? 1 : 0; /* Only enable first encoder by default */
    }
    
    /* Enable up to num_encoders */
    for (i = 0; i < num_encoders && i < MAX_ENCODERS; i++) {
        encoder_configs[i].enabled = 1;
    }
}

/* Parse a single configuration line */
static int parse_config_line(char *line)
{
    char *ptr, *name_ptr, *clk_ptr, *dt_ptr, *sw_ptr, *min_ptr, *max_ptr;
    int encoder_id;
    int clk_pin, dt_pin, sw_pin, min_range, max_range;
    
    /* Skip empty lines and comments */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || *line == '\n') {
        return 0; /* Skip this line */
    }
    
    /* Format: encoder_id=name:clk_pin:dt_pin:sw_pin:min_range:max_range */
    ptr = strchr(line, '=');
    if (!ptr) {
        return -1; /* Invalid format */
    }
    
    *ptr = '\0';
    encoder_id = simple_strtol(line, NULL, 10);
    if (encoder_id < 0 || encoder_id >= MAX_ENCODERS) {
        return -1; /* Invalid encoder ID */
    }
    
    /* Parse the configuration values */
    name_ptr = ptr + 1;
    clk_ptr = strchr(name_ptr, ':');
    if (!clk_ptr) return -1;
    *clk_ptr = '\0';
    clk_ptr++;
    
    dt_ptr = strchr(clk_ptr, ':');
    if (!dt_ptr) return -1;
    *dt_ptr = '\0';
    dt_ptr++;
    
    sw_ptr = strchr(dt_ptr, ':');
    if (!sw_ptr) return -1;
    *sw_ptr = '\0';
    sw_ptr++;
    
    min_ptr = strchr(sw_ptr, ':');
    if (!min_ptr) return -1;
    *min_ptr = '\0';
    min_ptr++;
    
    max_ptr = strchr(min_ptr, ':');
    if (!max_ptr) return -1;
    *max_ptr = '\0';
    max_ptr++;
    
    /* Remove newline from max_ptr if present */
    ptr = strchr(max_ptr, '\n');
    if (ptr) *ptr = '\0';
    
    /* Convert values */
    clk_pin = simple_strtol(clk_ptr, NULL, 10);
    dt_pin = simple_strtol(dt_ptr, NULL, 10);
    sw_pin = simple_strtol(sw_ptr, NULL, 10);
    min_range = simple_strtol(min_ptr, NULL, 10);
    max_range = simple_strtol(max_ptr, NULL, 10);
    
    /* Validate values */
    if (clk_pin < 0 || clk_pin > 27 || dt_pin < 0 || dt_pin > 27) {
        return -1; /* Invalid GPIO pins */
    }
    if (sw_pin < -1 || sw_pin > 27) {
        return -1; /* Invalid switch pin */
    }
    if (min_range >= max_range) {
        return -1; /* Invalid range */
    }
    
    /* Store configuration */
    strncpy(encoder_configs[encoder_id].name, name_ptr, sizeof(encoder_configs[encoder_id].name) - 1);
    encoder_configs[encoder_id].name[sizeof(encoder_configs[encoder_id].name) - 1] = '\0';
    encoder_configs[encoder_id].clk_pin = clk_pin;
    encoder_configs[encoder_id].dt_pin = dt_pin;
    encoder_configs[encoder_id].sw_pin = sw_pin;
    encoder_configs[encoder_id].min_range = min_range;
    encoder_configs[encoder_id].max_range = max_range;
    encoder_configs[encoder_id].enabled = 1;
    
    return 0;
}

/* Load configuration from file */
static int load_encoder_config(void)
{
    struct file *file;
    loff_t pos = 0;
    char *buffer, *line_start, *line_end;
    int ret = 0;
    ssize_t bytes_read;
    
    /* Initialize default configuration first */
    init_default_config();
    
    /* Try to open configuration file */
    file = filp_open(config_file, O_RDONLY, 0);
    if (IS_ERR(file)) {
        printk(KERN_WARNING "rotary_encoder: Configuration file '%s' not found, using default settings\n", config_file);
        return 0; /* Use defaults, this is not an error */
    }
    
    /* Allocate buffer for reading */
    buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buffer) {
        filp_close(file, NULL);
        printk(KERN_ERR "rotary_encoder: Failed to allocate buffer for configuration file\n");
        return -ENOMEM;
    }
    
    /* Read file content */
    bytes_read = kernel_read(file, buffer, PAGE_SIZE - 1, &pos);
    if (bytes_read < 0) {
        ret = bytes_read;
        printk(KERN_ERR "rotary_encoder: Failed to read configuration file: %d\n", ret);
        goto cleanup;
    }
    
    buffer[bytes_read] = '\0'; /* Null terminate */
    
    /* Parse line by line */
    line_start = buffer;
    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';
        
        if (parse_config_line(line_start) < 0) {
            printk(KERN_WARNING "rotary_encoder: Invalid configuration line: %s\n", line_start);
        }
        
        line_start = line_end + 1;
    }
    
    /* Handle last line if no newline at end */
    if (*line_start != '\0') {
        if (parse_config_line(line_start) < 0) {
            printk(KERN_WARNING "rotary_encoder: Invalid configuration line: %s\n", line_start);
        }
    }
    
    config_loaded = 1;
    printk(KERN_INFO "rotary_encoder: Configuration loaded from '%s'\n", config_file);
    
cleanup:
    kfree(buffer);
    filp_close(file, NULL);
    return ret;
}

/* Interrupt handler for CLK pin */
static irqreturn_t rotary_clk_interrupt(int irq, void *dev_id)
{
    struct rotary_encoder *enc = (struct rotary_encoder *)dev_id;
    unsigned long current_time = jiffies;
    int clk_state, dt_state;
    
    /* Debounce check */
    if (time_before(current_time, enc->last_interrupt_time + enc->debounce_time)) {
        return IRQ_HANDLED;
    }
    
    enc->last_interrupt_time = current_time;
    
    clk_state = gpio_get_value(enc->clk_gpio);
    dt_state = gpio_get_value(enc->dt_gpio);
    
    /* Only process on rising edge of CLK */
    if (clk_state == 1 && enc->last_clk == 0) {
        mutex_lock(&enc->lock);
        
        if (dt_state == 0) {
            /* Clockwise rotation */
            if (enc->position < enc->max_value) {
                enc->position++;
                enc->direction = 1;
            }
        } else {
            /* Counter-clockwise rotation */
            if (enc->position > enc->min_value) {
                enc->position--;
                enc->direction = -1;
            }
        }
        
        enc->data_ready = 1;
        mutex_unlock(&enc->lock);
        wake_up_interruptible(&enc->wait_queue);
    }
    
    enc->last_clk = clk_state;
    enc->last_dt = dt_state;
    
    return IRQ_HANDLED;
}

/* Interrupt handler for switch pin */
static irqreturn_t rotary_sw_interrupt(int irq, void *dev_id)
{
    struct rotary_encoder *enc = (struct rotary_encoder *)dev_id;
    unsigned long current_time = jiffies;
    int sw_state;
    
    /* Debounce check */
    if (time_before(current_time, enc->button_last_time + enc->debounce_time)) {
        return IRQ_HANDLED;
    }
    
    enc->button_last_time = current_time;
    sw_state = gpio_get_value(enc->sw_gpio);
    
    /* Button press on falling edge (assuming pull-up) */
    if (sw_state == 0 && enc->button_last_state == 1) {
        mutex_lock(&enc->lock);
        enc->button_pressed = 1;
        enc->data_ready = 1;
        mutex_unlock(&enc->lock);
        wake_up_interruptible(&enc->wait_queue);
    }
    
    enc->button_last_state = sw_state;
    
    return IRQ_HANDLED;
}

/* Device file operations */
static int rotary_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "rotary_encoder: Device opened\n");
    return 0;
}

static int rotary_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "rotary_encoder: Device closed\n");
    return 0;
}

static ssize_t rotary_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    struct rotary_status status[MAX_ENCODERS];
    int i, bytes_to_copy;
    
    if (len < sizeof(struct rotary_status)) {
        return -EINVAL;
    }
    
    bytes_to_copy = min(len, sizeof(status));
    
    /* Fill status for all active encoders */
    for (i = 0; i < num_encoders; i++) {
        mutex_lock(&encoders[i].lock);
        status[i].encoder_id = i;
        status[i].position = encoders[i].position;
        status[i].direction = encoders[i].direction;
        status[i].button_pressed = encoders[i].button_pressed;
        status[i].timestamp = jiffies;
        strncpy(status[i].name, encoders[i].name, sizeof(status[i].name) - 1);
        status[i].name[sizeof(status[i].name) - 1] = '\0';
        
        /* Clear the flags after reading */
        encoders[i].direction = 0;
        encoders[i].button_pressed = 0;
        encoders[i].data_ready = 0;
        mutex_unlock(&encoders[i].lock);
    }
    
    if (copy_to_user(buffer, status, bytes_to_copy)) {
        return -EFAULT;
    }
    
    return bytes_to_copy;
}

static long rotary_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct rotary_range range;
    struct rotary_status status;
    int encoder_id, debounce;
    
    switch (cmd) {
        case ROTARY_SET_RANGE:
            if (copy_from_user(&range, (void __user *)arg, sizeof(range))) {
                return -EFAULT;
            }
            
            if (range.encoder_id < 0 || range.encoder_id >= num_encoders) {
                return -EINVAL;
            }
            
            if (range.min_value >= range.max_value) {
                return -EINVAL;
            }
            
            mutex_lock(&encoders[range.encoder_id].lock);
            encoders[range.encoder_id].min_value = range.min_value;
            encoders[range.encoder_id].max_value = range.max_value;
            
            /* Clamp current position to new range */
            if (encoders[range.encoder_id].position < range.min_value) {
                encoders[range.encoder_id].position = range.min_value;
            } else if (encoders[range.encoder_id].position > range.max_value) {
                encoders[range.encoder_id].position = range.max_value;
            }
            mutex_unlock(&encoders[range.encoder_id].lock);
            break;
            
        case ROTARY_GET_POSITION:
            if (copy_from_user(&status, (void __user *)arg, sizeof(status))) {
                return -EFAULT;
            }
            
            if (status.encoder_id < 0 || status.encoder_id >= num_encoders) {
                return -EINVAL;
            }
            
            mutex_lock(&encoders[status.encoder_id].lock);
            status.position = encoders[status.encoder_id].position;
            status.direction = encoders[status.encoder_id].direction;
            status.button_pressed = encoders[status.encoder_id].button_pressed;
            status.timestamp = jiffies;
            strncpy(status.name, encoders[status.encoder_id].name, sizeof(status.name) - 1);
            status.name[sizeof(status.name) - 1] = '\0';
            mutex_unlock(&encoders[status.encoder_id].lock);
            
            if (copy_to_user((void __user *)arg, &status, sizeof(status))) {
                return -EFAULT;
            }
            break;
            
        case ROTARY_RESET:
            if (copy_from_user(&encoder_id, (void __user *)arg, sizeof(encoder_id))) {
                return -EFAULT;
            }
            
            if (encoder_id < 0 || encoder_id >= num_encoders) {
                return -EINVAL;
            }
            
            mutex_lock(&encoders[encoder_id].lock);
            encoders[encoder_id].position = 0;
            encoders[encoder_id].direction = 0;
            encoders[encoder_id].button_pressed = 0;
            mutex_unlock(&encoders[encoder_id].lock);
            break;
            
        case ROTARY_SET_DEBOUNCE:
            if (copy_from_user(&debounce, (void __user *)arg, sizeof(debounce))) {
                return -EFAULT;
            }
            
            if (debounce < 0 || debounce > 100) {
                return -EINVAL;
            }
            
            /* Apply to all encoders */
            for (encoder_id = 0; encoder_id < num_encoders; encoder_id++) {
                encoders[encoder_id].debounce_time = msecs_to_jiffies(debounce);
            }
            break;
            
        default:
            return -ENOTTY;
    }
    
    return 0;
}

static unsigned int rotary_poll(struct file *file, poll_table *wait)
{
    int i;
    unsigned int mask = 0;
    
    for (i = 0; i < num_encoders; i++) {
        poll_wait(file, &encoders[i].wait_queue, wait);
        if (encoders[i].data_ready) {
            mask |= POLLIN | POLLRDNORM;
            break;
        }
    }
    
    return mask;
}

static struct file_operations rotary_fops = {
    .owner = THIS_MODULE,
    .open = rotary_open,
    .release = rotary_release,
    .read = rotary_read,
    .unlocked_ioctl = rotary_ioctl,
    .poll = rotary_poll,
};

/* Initialize GPIO and interrupts for an encoder */
static int init_encoder(int id)
{
    struct rotary_encoder *enc = &encoders[id];
    int ret;
    
    /* Initialize encoder structure from configuration */
    strncpy(enc->name, encoder_configs[id].name, sizeof(enc->name) - 1);
    enc->name[sizeof(enc->name) - 1] = '\0';
    enc->clk_gpio = encoder_configs[id].clk_pin;
    enc->dt_gpio = encoder_configs[id].dt_pin;
    enc->sw_gpio = encoder_configs[id].sw_pin;
    enc->position = 0;
    enc->min_value = encoder_configs[id].min_range;
    enc->max_value = encoder_configs[id].max_range;
    enc->last_clk = 0;
    enc->last_dt = 0;
    enc->direction = 0;
    enc->button_pressed = 0;
    enc->button_last_state = 1; /* Assuming pull-up */
    enc->last_interrupt_time = 0;
    enc->button_last_time = 0;
    enc->debounce_time = msecs_to_jiffies(10); /* 10ms default debounce */
    enc->data_ready = 0;
    
    mutex_init(&enc->lock);
    init_waitqueue_head(&enc->wait_queue);
    
    /* Request and configure CLK GPIO */
    ret = gpio_request(enc->clk_gpio, "rotary_clk");
    if (ret) {
        printk(KERN_ERR "rotary_encoder: Failed to request CLK GPIO %d\n", enc->clk_gpio);
        return ret;
    }
    gpio_direction_input(enc->clk_gpio);
    gpio_set_debounce(enc->clk_gpio, 0);
    
    /* Request and configure DT GPIO */
    ret = gpio_request(enc->dt_gpio, "rotary_dt");
    if (ret) {
        printk(KERN_ERR "rotary_encoder: Failed to request DT GPIO %d\n", enc->dt_gpio);
        gpio_free(enc->clk_gpio);
        return ret;
    }
    gpio_direction_input(enc->dt_gpio);
    gpio_set_debounce(enc->dt_gpio, 0);
    
    /* Request and configure SW GPIO if specified */
    if (enc->sw_gpio >= 0) {
        ret = gpio_request(enc->sw_gpio, "rotary_sw");
        if (ret) {
            printk(KERN_ERR "rotary_encoder: Failed to request SW GPIO %d\n", enc->sw_gpio);
            gpio_free(enc->dt_gpio);
            gpio_free(enc->clk_gpio);
            return ret;
        }
        gpio_direction_input(enc->sw_gpio);
        gpio_set_debounce(enc->sw_gpio, 0);
        
        /* Setup switch interrupt */
        ret = request_irq(gpio_to_irq(enc->sw_gpio), rotary_sw_interrupt,
                         IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                         "rotary_sw", enc);
        if (ret) {
            printk(KERN_ERR "rotary_encoder: Failed to request SW IRQ\n");
            gpio_free(enc->sw_gpio);
            gpio_free(enc->dt_gpio);
            gpio_free(enc->clk_gpio);
            return ret;
        }
    }
    
    /* Setup CLK interrupt */
    ret = request_irq(gpio_to_irq(enc->clk_gpio), rotary_clk_interrupt,
                     IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                     "rotary_clk", enc);
    if (ret) {
        printk(KERN_ERR "rotary_encoder: Failed to request CLK IRQ\n");
        if (enc->sw_gpio >= 0) {
            free_irq(gpio_to_irq(enc->sw_gpio), enc);
            gpio_free(enc->sw_gpio);
        }
        gpio_free(enc->dt_gpio);
        gpio_free(enc->clk_gpio);
        return ret;
    }
    
    /* Initialize pin states */
    enc->last_clk = gpio_get_value(enc->clk_gpio);
    enc->last_dt = gpio_get_value(enc->dt_gpio);
    if (enc->sw_gpio >= 0) {
        enc->button_last_state = gpio_get_value(enc->sw_gpio);
    }
    
    printk(KERN_INFO "rotary_encoder: Encoder %d initialized (CLK=%d, DT=%d, SW=%d)\n",
           id, enc->clk_gpio, enc->dt_gpio, enc->sw_gpio);
    
    return 0;
}

/* Cleanup encoder */
static void cleanup_encoder(int id)
{
    struct rotary_encoder *enc = &encoders[id];
    
    if (enc->clk_gpio >= 0) {
        free_irq(gpio_to_irq(enc->clk_gpio), enc);
        gpio_free(enc->clk_gpio);
    }
    
    if (enc->dt_gpio >= 0) {
        gpio_free(enc->dt_gpio);
    }
    
    if (enc->sw_gpio >= 0) {
        free_irq(gpio_to_irq(enc->sw_gpio), enc);
        gpio_free(enc->sw_gpio);
    }
    
    mutex_destroy(&enc->lock);
}

/* Module initialization */
static int __init rotary_init(void)
{
    int ret, i;
    
    printk(KERN_INFO "rotary_encoder: Initializing driver\n");
    
    /* Load configuration file */
    ret = load_encoder_config();
    if (ret < 0) {
        printk(KERN_ERR "rotary_encoder: Failed to load configuration: %d\n", ret);
        return ret;
    }
    
    /* Count enabled encoders if using config file */
    if (config_loaded) {
        int enabled_count = 0;
        for (i = 0; i < MAX_ENCODERS; i++) {
            if (encoder_configs[i].enabled) {
                enabled_count++;
            }
        }
        if (enabled_count > 0) {
            num_encoders = enabled_count;
        }
    }
    
    /* Validate parameters */
    if (num_encoders < 1 || num_encoders > MAX_ENCODERS) {
        printk(KERN_ERR "rotary_encoder: Invalid number of encoders (%d)\n", num_encoders);
        return -EINVAL;
    }
    
    /* Allocate device number */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "rotary_encoder: Failed to allocate device number\n");
        return ret;
    }
    
    /* Create device class */
    rotary_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(rotary_class)) {
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ERR "rotary_encoder: Failed to create device class\n");
        return PTR_ERR(rotary_class);
    }
    
    /* Initialize character device */
    cdev_init(&rotary_cdev, &rotary_fops);
    ret = cdev_add(&rotary_cdev, dev_number, 1);
    if (ret < 0) {
        class_destroy(rotary_class);
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ERR "rotary_encoder: Failed to add character device\n");
        return ret;
    }
    
    /* Create device file */
    if (device_create(rotary_class, NULL, dev_number, NULL, DEVICE_NAME) == NULL) {
        cdev_del(&rotary_cdev);
        class_destroy(rotary_class);
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ERR "rotary_encoder: Failed to create device file\n");
        return -1;
    }
    
    /* Initialize encoders */
    if (config_loaded) {
        /* Initialize only enabled encoders from config */
        for (i = 0; i < MAX_ENCODERS; i++) {
            if (encoder_configs[i].enabled) {
                ret = init_encoder(i);
                if (ret) {
                    /* Cleanup already initialized encoders */
                    int j;
                    for (j = 0; j < i; j++) {
                        if (encoder_configs[j].enabled) {
                            cleanup_encoder(j);
                        }
                    }
                    device_destroy(rotary_class, dev_number);
                    cdev_del(&rotary_cdev);
                    class_destroy(rotary_class);
                    unregister_chrdev_region(dev_number, 1);
                    return ret;
                }
            }
        }
    } else {
        /* Initialize encoders using legacy method */
        for (i = 0; i < num_encoders; i++) {
            ret = init_encoder(i);
            if (ret) {
                /* Cleanup already initialized encoders */
                while (--i >= 0) {
                    cleanup_encoder(i);
                }
                device_destroy(rotary_class, dev_number);
                cdev_del(&rotary_cdev);
                class_destroy(rotary_class);
                unregister_chrdev_region(dev_number, 1);
                return ret;
            }
        }
    }
    
    if (config_loaded) {
        printk(KERN_INFO "rotary_encoder: Driver initialized with %d encoder(s) from configuration file\n", num_encoders);
        for (i = 0; i < MAX_ENCODERS; i++) {
            if (encoder_configs[i].enabled) {
                printk(KERN_INFO "rotary_encoder: Encoder %d ('%s'): CLK=%d, DT=%d, SW=%d, range=%d to %d\n",
                       i, encoder_configs[i].name, encoder_configs[i].clk_pin, 
                       encoder_configs[i].dt_pin, encoder_configs[i].sw_pin,
                       encoder_configs[i].min_range, encoder_configs[i].max_range);
            }
        }
    } else {
        printk(KERN_INFO "rotary_encoder: Driver initialized with %d encoder(s) using default configuration\n", num_encoders);
    }
    return 0;
}

/* Module cleanup */
static void __exit rotary_exit(void)
{
    int i;
    
    printk(KERN_INFO "rotary_encoder: Cleaning up driver\n");
    
    /* Cleanup all encoders */
    if (config_loaded) {
        for (i = 0; i < MAX_ENCODERS; i++) {
            if (encoder_configs[i].enabled) {
                cleanup_encoder(i);
            }
        }
    } else {
        for (i = 0; i < num_encoders; i++) {
            cleanup_encoder(i);
        }
    }
    
    /* Remove device and class */
    device_destroy(rotary_class, dev_number);
    cdev_del(&rotary_cdev);
    class_destroy(rotary_class);
    unregister_chrdev_region(dev_number, 1);
    
    printk(KERN_INFO "rotary_encoder: Driver cleanup complete\n");
}

module_init(rotary_init);
module_exit(rotary_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Mark Mackelprang");
MODULE_DESCRIPTION("Raspberry Pi Rotary Encoder Device Driver");
MODULE_VERSION("1.0");
/*
 * Raspberry Pi Rotary Encoder Driver - User Space Header
 * 
 * This header file contains the data structures and IOCTL commands
 * for communicating with the rotary encoder kernel module.
 *
 * Author: Mark Mackelprang
 * License: MIT
 */

#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <linux/ioctl.h>

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
    char name[32];  /* Encoder name from configuration */
};

#endif /* ROTARY_ENCODER_H */
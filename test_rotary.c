/*
 * Example application for testing the Raspberry Pi Rotary Encoder Driver
 *
 * This application demonstrates how to use the rotary encoder driver
 * from user space, including setting ranges, reading positions, and
 * handling button presses.
 *
 * Compile: gcc -o test_rotary test_rotary.c
 * Usage: ./test_rotary [encoder_id]
 *
 * Author: Mark Mackelprang
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include "rotary_encoder.h"

static int running = 1;

void signal_handler(int sig)
{
    running = 0;
    printf("\nShutting down...\n");
}

int main(int argc, char *argv[])
{
    int fd;
    int encoder_id = 0;
    struct rotary_range range;
    struct rotary_status status[MAX_ENCODERS];
    struct pollfd pfd;
    int ret;
    
    /* Parse command line arguments */
    if (argc > 1) {
        encoder_id = atoi(argv[1]);
        if (encoder_id < 0 || encoder_id >= MAX_ENCODERS) {
            fprintf(stderr, "Error: Invalid encoder ID %d (must be 0-%d)\n", 
                    encoder_id, MAX_ENCODERS - 1);
            return 1;
        }
    }
    
    /* Install signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Open the device */
    fd = open("/dev/rotary_encoder", O_RDWR);
    if (fd < 0) {
        perror("Error opening device");
        fprintf(stderr, "Make sure the rotary_encoder module is loaded and /dev/rotary_encoder exists\n");
        return 1;
    }
    
    printf("Rotary Encoder Test Application\n");
    printf("Monitoring encoder %d\n", encoder_id);
    printf("Press Ctrl+C to exit\n\n");
    
    /* Set a range for the encoder (example: -100 to 100) */
    range.encoder_id = encoder_id;
    range.min_value = -100;
    range.max_value = 100;
    
    ret = ioctl(fd, ROTARY_SET_RANGE, &range);
    if (ret < 0) {
        perror("Error setting range");
        close(fd);
        return 1;
    }
    printf("Set range for encoder %d: %d to %d\n", encoder_id, range.min_value, range.max_value);
    
    /* Reset encoder position */
    ret = ioctl(fd, ROTARY_RESET, &encoder_id);
    if (ret < 0) {
        perror("Error resetting encoder");
        close(fd);
        return 1;
    }
    printf("Reset encoder %d position to 0\n\n", encoder_id);
    
    /* Set up polling */
    pfd.fd = fd;
    pfd.events = POLLIN;
    
    /* Main loop */
    while (running) {
        /* Poll for data with 1 second timeout */
        ret = poll(&pfd, 1, 1000);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted by signal */
            }
            perror("Poll error");
            break;
        }
        
        if (ret == 0) {
            /* Timeout - just continue to check for signals */
            continue;
        }
        
        if (pfd.revents & POLLIN) {
            /* Data available - read status */
            ret = read(fd, status, sizeof(status));
            if (ret < 0) {
                perror("Error reading from device");
                break;
            }
            
            /* Display status for all encoders with changes */
            int num_read = ret / sizeof(struct rotary_status);
            for (int i = 0; i < num_read; i++) {
                if (status[i].direction != 0 || status[i].button_pressed) {
                    printf("Encoder %d: Position=%d", status[i].encoder_id, status[i].position);
                    
                    if (status[i].direction > 0) {
                        printf(" (Clockwise)");
                    } else if (status[i].direction < 0) {
                        printf(" (Counter-clockwise)");
                    }
                    
                    if (status[i].button_pressed) {
                        printf(" [BUTTON PRESSED]");
                    }
                    
                    printf("\n");
                }
            }
        }
    }
    
    close(fd);
    printf("Application terminated.\n");
    return 0;
}
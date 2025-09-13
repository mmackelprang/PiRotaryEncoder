/*
 * Simple Volume Control Example
 *
 * This example demonstrates using a rotary encoder as a volume control,
 * with the button for mute/unmute functionality.
 *
 * Compile: gcc -o volume_control volume_control.c
 * Usage: sudo ./volume_control
 *
 * Author: Mark Mackelprang
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include "../rotary_encoder.h"

static int running = 1;
static int muted = 0;
static int volume = 50; /* Current volume (0-100) */

void signal_handler(int sig)
{
    running = 0;
}

void set_system_volume(int vol)
{
    char cmd[100];
    if (muted) {
        printf("Volume: %d%% (MUTED)\n", vol);
    } else {
        printf("Volume: %d%%\n", vol);
        /* Use ALSA to set actual system volume */
        snprintf(cmd, sizeof(cmd), "amixer set Master %d%% > /dev/null 2>&1", vol);
        system(cmd);
    }
}

void toggle_mute()
{
    muted = !muted;
    if (muted) {
        printf("MUTED\n");
        system("amixer set Master mute > /dev/null 2>&1");
    } else {
        printf("UNMUTED\n");
        system("amixer set Master unmute > /dev/null 2>&1");
        set_system_volume(volume);
    }
}

int main()
{
    int fd;
    struct rotary_range range;
    struct rotary_status status[MAX_ENCODERS];
    struct pollfd pfd;
    int ret;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Open device */
    fd = open("/dev/rotary_encoder", O_RDWR);
    if (fd < 0) {
        perror("Error opening device");
        return 1;
    }
    
    printf("Volume Control - Rotary Encoder Example\n");
    printf("Turn encoder to adjust volume (0-100%%)\n");
    printf("Press button to mute/unmute\n");
    printf("Press Ctrl+C to exit\n\n");
    
    /* Set volume range (0-100) */
    range.encoder_id = 0;
    range.min_value = 0;
    range.max_value = 100;
    ioctl(fd, ROTARY_SET_RANGE, &range);
    
    /* Set initial position to current volume */
    /* Reset first, then adjust to current volume would require multiple steps */
    ioctl(fd, ROTARY_RESET, &range.encoder_id);
    
    /* Set initial volume */
    set_system_volume(volume);
    
    /* Setup polling */
    pfd.fd = fd;
    pfd.events = POLLIN;
    
    while (running) {
        ret = poll(&pfd, 1, 1000);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("Poll error");
            break;
        }
        
        if (ret == 0) continue; /* Timeout */
        
        if (pfd.revents & POLLIN) {
            ret = read(fd, status, sizeof(status));
            if (ret < 0) {
                perror("Read error");
                break;
            }
            
            /* Process encoder 0 events */
            if (status[0].direction != 0) {
                /* Volume changed */
                volume = status[0].position;
                if (!muted) {
                    set_system_volume(volume);
                } else {
                    printf("Volume: %d%% (MUTED)\n", volume);
                }
            }
            
            if (status[0].button_pressed) {
                /* Mute/unmute toggle */
                toggle_mute();
            }
        }
    }
    
    close(fd);
    printf("\nVolume control terminated.\n");
    return 0;
}
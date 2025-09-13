# Raspberry Pi Rotary Encoder Device Driver

A Linux kernel module for the Raspberry Pi that supports up to 4 rotary encoders with configurable ranges, button press detection, and real-time position tracking.

## Features

- **Multi-encoder support**: Handle up to 4 rotary encoders simultaneously
- **Configurable ranges**: Set custom min/max values per encoder
- **Button detection**: Integrated push-button support with debouncing
- **Real-time tracking**: Position and direction monitoring with interrupt-driven updates
- **Non-blocking I/O**: Poll-based interface for efficient event handling
- **IOCTL interface**: Advanced configuration and control
- **Device tree support**: Easy hardware configuration

## Quick Start

1. **Build and install the driver:**
   ```bash
   make
   sudo make install
   ```

2. **Load the module:**
   ```bash
   sudo make load
   ```

3. **Test with the example application:**
   ```bash
   gcc -o test_rotary test_rotary.c
   sudo ./test_rotary
   ```

## Hardware Connection

### Basic Single Encoder Setup

Connect a KY-040 rotary encoder to your Raspberry Pi:

```
Rotary Encoder    Raspberry Pi
--------------    ------------
CLK    ------>    GPIO 18
DT     ------>    GPIO 19  
SW     ------>    GPIO 20
+      ------>    3.3V
GND    ------>    GND
```

### Multiple Encoder Setup

Default GPIO assignments for up to 4 encoders:

| Encoder | CLK GPIO | DT GPIO | SW GPIO |
|---------|----------|---------|---------|
| 0       | 18       | 19      | 20      |
| 1       | 22       | 23      | -       |
| 2       | 24       | 25      | -       |
| 3       | 26       | 27      | -       |

*Note: SW GPIO of -1 means no button support for that encoder*

## Installation

### Prerequisites

- Raspberry Pi with Raspbian/Raspberry Pi OS
- Kernel headers: `sudo apt install raspberrypi-kernel-headers`
- Build tools: `sudo apt install build-essential`

### Building

```bash
# Clone the repository
git clone https://github.com/mmackelprang/PiRotaryEncoder.git
cd PiRotaryEncoder

# Build the kernel module
make

# Install the module (optional)
sudo make install
```

### Loading the Module

```bash
# Load with default settings (1 encoder on pins 18,19,20)
sudo insmod rotary_encoder.ko

# Load with custom configuration
sudo insmod rotary_encoder.ko num_encoders=2 clk_pins=18,22 dt_pins=19,23 sw_pins=20,-1

# Or use make targets
sudo make load    # Load module
sudo make unload  # Unload module
sudo make reload  # Reload module
```

### Module Parameters

- `num_encoders`: Number of encoders to initialize (1-4, default: 1)
- `clk_pins`: Array of CLK GPIO pins (default: 18,22,24,26)
- `dt_pins`: Array of DT GPIO pins (default: 19,23,25,27)  
- `sw_pins`: Array of SW GPIO pins (default: 20,-1,-1,-1, -1=no button)

## Usage

### Device Interface

The driver creates `/dev/rotary_encoder` device file. Applications can:

- **Read**: Get current status of all encoders
- **Poll**: Wait for encoder events non-blocking
- **IOCTL**: Configure ranges, reset positions, set debounce

### C Programming Interface

```c
#include "rotary_encoder.h"

int fd = open("/dev/rotary_encoder", O_RDWR);

// Set encoder range
struct rotary_range range = {
    .encoder_id = 0,
    .min_value = -100,
    .max_value = 100
};
ioctl(fd, ROTARY_SET_RANGE, &range);

// Read encoder status
struct rotary_status status[MAX_ENCODERS];
read(fd, status, sizeof(status));

// Reset encoder
int encoder_id = 0;
ioctl(fd, ROTARY_RESET, &encoder_id);
```

### Python Interface

```python
import fcntl
import struct
import select

# IOCTL commands
ROTARY_SET_RANGE = 0x40084801
ROTARY_GET_POSITION = 0x80084802
ROTARY_RESET = 0x40044803

# Open device
fd = open('/dev/rotary_encoder', 'rb+', buffering=0)

# Set range
range_data = struct.pack('iii', 0, -50, 50)  # encoder_id, min, max
fcntl.ioctl(fd, ROTARY_SET_RANGE, range_data)

# Poll for events
while True:
    ready, _, _ = select.select([fd], [], [], 1.0)
    if ready:
        data = fd.read(20)  # sizeof(rotary_status)
        encoder_id, position, direction, button, timestamp = struct.unpack('iiiIL', data)
        print(f"Encoder {encoder_id}: pos={position}, dir={direction}, btn={button}")
```

### Shell Scripting

```bash
# Monitor encoder events
cat /dev/rotary_encoder | hexdump -C

# Check device status
ls -l /dev/rotary_encoder

# View kernel messages
dmesg | grep rotary_encoder
```

## API Reference

### Data Structures

```c
struct rotary_range {
    int encoder_id;     // Encoder ID (0-3)
    int min_value;      // Minimum position value
    int max_value;      // Maximum position value
};

struct rotary_status {
    int encoder_id;     // Encoder ID (0-3)
    int position;       // Current position
    int direction;      // -1=CCW, 1=CW, 0=no change
    int button_pressed; // 1=pressed, 0=not pressed
    unsigned long timestamp; // Kernel timestamp (jiffies)
};
```

### IOCTL Commands

- `ROTARY_SET_RANGE`: Set min/max range for an encoder
- `ROTARY_GET_POSITION`: Get current status of an encoder
- `ROTARY_RESET`: Reset encoder position to 0
- `ROTARY_SET_DEBOUNCE`: Set debounce time in milliseconds (0-100ms)

## Theory of Operation

### Rotary Encoder Basics

Rotary encoders use two output signals (CLK and DT) that are 90° out of phase. By monitoring the sequence of these signals, we can determine:

- **Rotation direction**: CLK leads DT for clockwise, DT leads CLK for counter-clockwise
- **Rotation speed**: Faster rotation = higher interrupt frequency
- **Position tracking**: Increment/decrement counter based on direction

### Quadrature Encoding

The driver implements quadrature decoding:

```
Clockwise:     CLK: ¯¯¯\_\_\_/¯¯¯\_\_\_/
               DT:  \_\_\_/¯¯¯\_\_\_/¯¯¯\

Counter-CW:    CLK: ¯¯¯\_\_\_/¯¯¯\_\_\_/
               DT:  /¯¯¯\_\_\_/¯¯¯\_\_\_
```

### Interrupt Handling

- **CLK pin**: Rising/falling edge interrupts for position tracking
- **SW pin**: Falling edge interrupt for button presses (assuming pull-up)
- **Debouncing**: Software debouncing prevents false triggers from mechanical noise

### Range Management

Each encoder maintains configurable min/max bounds:
- Position clamps to range limits
- Prevents overflow/underflow
- Supports negative ranges for bidirectional controls

## Troubleshooting

### Common Issues

1. **Module won't load**
   ```bash
   # Check kernel headers
   ls /lib/modules/$(uname -r)/build
   
   # Install if missing
   sudo apt install raspberrypi-kernel-headers
   ```

2. **GPIO permission errors**
   ```bash
   # Check GPIO availability
   cat /sys/kernel/debug/gpio
   
   # Ensure pins aren't in use
   sudo gpio readall
   ```

3. **No device file**
   ```bash
   # Check if module loaded
   lsmod | grep rotary_encoder
   
   # Check device creation
   ls -l /dev/rotary_encoder
   
   # Manual device creation if needed
   sudo mknod /dev/rotary_encoder c $(cat /proc/devices | grep rotary_encoder | cut -d' ' -f1) 0
   ```

4. **Erratic readings**
   - Check wiring connections
   - Increase debounce time: `echo 20 > /sys/module/rotary_encoder/parameters/debounce`
   - Verify power supply stability

### Debug Information

```bash
# View module parameters
cat /sys/module/rotary_encoder/parameters/*

# Check GPIO states
cat /sys/kernel/debug/gpio | grep -A5 -B5 "gpio-18\|gpio-19\|gpio-20"

# Monitor kernel messages
sudo dmesg -w | grep rotary_encoder

# Check interrupts
cat /proc/interrupts | grep rotary
```

## Examples

See the `examples/` directory for:
- Basic position monitoring
- Multi-encoder applications  
- Volume control implementation
- Menu navigation system
- Python bindings

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make changes and test thoroughly
4. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Author

Mark Mackelprang

## References

- [Linux GPIO Subsystem](https://www.kernel.org/doc/Documentation/gpio/)
- [Rotary Encoder Theory](https://en.wikipedia.org/wiki/Rotary_encoder)
- [Raspberry Pi GPIO](https://www.raspberrypi.org/documentation/usage/gpio/)

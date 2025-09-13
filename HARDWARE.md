# Hardware Connection Guide

This document provides detailed information on connecting rotary encoders to the Raspberry Pi for use with the Rotary Encoder Device Driver.

## Supported Hardware

### Recommended Rotary Encoders

- **KY-040 Rotary Encoder Module** (most common)
- **EC11 Series Encoders** 
- **PEC11 Series Encoders**
- Any incremental rotary encoder with quadrature output

### Requirements

- 2-bit quadrature output (CLK and DT pins)
- Optional integrated push button (SW pin)
- 3.3V or 5V compatible (Raspberry Pi is 3.3V)
- Pull-up resistors (often integrated on modules)

## Basic Wiring

### Single Encoder (KY-040)

```
KY-040 Module     Raspberry Pi
-------------     ------------
GND      <--->   GND (Pin 6, 9, 14, 20, 25, 30, 34, 39)
+        <--->   3.3V (Pin 1, 17) or 5V (Pin 2, 4)
SW       <--->   GPIO 20 (Pin 38)
DT       <--->   GPIO 19 (Pin 35)  
CLK      <--->   GPIO 18 (Pin 12)
```

### Physical Pin Layout

```
Raspberry Pi GPIO Pinout (40-pin header):
   3.3V  1 ┃ 2  5V
  GPIO2  3 ┃ 4  5V  
  GPIO3  5 ┃ 6  GND
  GPIO4  7 ┃ 8  GPIO14
    GND  9 ┃10  GPIO15
 GPIO17 11 ┃12  GPIO18  <- CLK (default)
 GPIO27 13 ┃14  GND
 GPIO22 15 ┃16  GPIO23
   3.3V 17 ┃18  GPIO24
 GPIO10 19 ┃20  GND
  GPIO9 21 ┃22  GPIO25
 GPIO11 23 ┃24  GPIO8
    GND 25 ┃26  GPIO7
  GPIO0 27 ┃28  GPIO1
  GPIO5 29 ┃30  GND
  GPIO6 31 ┃32  GPIO12
 GPIO13 33 ┃34  GND
 GPIO19 35 ┃36  GPIO16  <- DT (default)
 GPIO26 37 ┃38  GPIO20  <- SW (default)
    GND 39 ┃40  GPIO21
```

## Multiple Encoder Setup

### Default GPIO Assignments

| Encoder | CLK GPIO | DT GPIO | SW GPIO | CLK Pin | DT Pin | SW Pin |
|---------|----------|---------|---------|---------|---------|---------|
| 0       | 18       | 19      | 20      | 12      | 35     | 38      |
| 1       | 22       | 23      | -       | 15      | 16     | -       |
| 2       | 24       | 25      | -       | 18      | 22     | -       |
| 3       | 26       | 27      | -       | 37      | 13     | -       |

### Wiring Diagram for 4 Encoders

```
Encoder 0:          Encoder 1:          Encoder 2:          Encoder 3:
CLK -> GPIO18       CLK -> GPIO22       CLK -> GPIO24       CLK -> GPIO26
DT  -> GPIO19       DT  -> GPIO23       DT  -> GPIO25       DT  -> GPIO27  
SW  -> GPIO20       SW  -> (none)       SW  -> (none)       SW  -> (none)
+   -> 3.3V         +   -> 3.3V         +   -> 3.3V         +   -> 3.3V
GND -> GND          GND -> GND          GND -> GND          GND -> GND
```

### Custom GPIO Assignment

You can specify custom GPIO pins when loading the module:

```bash
# Example: Custom pin assignment
sudo insmod rotary_encoder.ko \
    num_encoders=2 \
    clk_pins=17,27 \
    dt_pins=18,22 \
    sw_pins=19,-1
```

## Power Considerations

### Voltage Levels

- **Raspberry Pi GPIO**: 3.3V logic levels
- **Input tolerance**: 3.3V (NOT 5V tolerant!)
- **Current per pin**: Maximum 16mA

### Power Supply Options

1. **3.3V Supply** (Recommended)
   - Connect encoder + to Pi 3.3V pin
   - Most reliable and safe option

2. **5V Supply** (With level shifting)
   - Only if encoder requires 5V
   - Must use level shifters for GPIO signals
   - Risk of damage if connected directly

### Pull-up Resistors

Most encoder modules include built-in pull-up resistors (~10kΩ). If using a bare encoder:

```
3.3V ----[10kΩ]---- CLK ---- Encoder CLK
3.3V ----[10kΩ]---- DT  ---- Encoder DT
3.3V ----[10kΩ]---- SW  ---- Encoder SW
```

## Signal Quality

### Proper Grounding

- **Star grounding**: Connect all encoder grounds to a single Pi GND pin
- **Short wires**: Keep connections under 20cm when possible
- **Twisted pairs**: Use twisted wire pairs for CLK/DT and power/ground

### Noise Reduction

1. **Capacitive filtering**: 100nF ceramic capacitors near encoder
2. **Ferrite cores**: On longer cables to reduce EMI
3. **Shielded cables**: For installations with electrical noise

### Example with Filtering

```
Encoder Module:
    CLK ----[100nF to GND]---- GPIO18
    DT  ----[100nF to GND]---- GPIO19
    SW  ----[100nF to GND]---- GPIO20
```

## Mechanical Considerations

### Mounting

- **Rigid mounting**: Prevents mechanical noise from vibration
- **Panel mount**: Use encoders designed for panel mounting
- **Shaft alignment**: Ensure proper shaft alignment to prevent binding

### Encoder Types

1. **Detented encoders**: Provide tactile feedback (most common)
   - Steps per revolution: 20-24 (typical)
   - Good for user interfaces

2. **Non-detented encoders**: Smooth rotation
   - Higher resolution possible
   - Better for precise control

3. **Encoder with switch**: Combined rotary + push button
   - Ideal for menu navigation
   - Single unit reduces wiring

## Troubleshooting Hardware Issues

### No Response from Encoder

1. **Check connections**:
   ```bash
   # Test GPIO pin states
   gpio readall
   # or
   cat /sys/kernel/debug/gpio
   ```

2. **Verify power supply**:
   ```bash
   # Check voltage at encoder pins with multimeter
   # Should read 3.3V between + and GND
   ```

3. **Test continuity**:
   ```bash
   # Use multimeter to verify wire connections
   # Check for broken wires or bad solder joints
   ```

### Erratic Readings

1. **Mechanical issues**:
   - Check for loose mounting
   - Verify shaft alignment
   - Clean encoder contacts

2. **Electrical noise**:
   - Add capacitive filtering
   - Shorten wire lengths
   - Check for nearby interference sources

3. **Software debouncing**:
   ```bash
   # Increase debounce time
   echo 20 > /sys/module/rotary_encoder/parameters/debounce
   ```

### Inconsistent Direction

1. **Wiring**: Swap CLK and DT connections
2. **Pull-ups**: Verify pull-up resistors are present
3. **Signal levels**: Check for proper 3.3V logic levels

## Advanced Configurations

### High-Resolution Encoders

For encoders with >100 steps per revolution:

```bash
# Load with higher precision debounce
sudo insmod rotary_encoder.ko debounce_ms=5
```

### Industrial Environments

For noisy industrial environments:

1. **Optical isolation**: Use opto-isolators for CLK/DT signals
2. **Differential signaling**: Use encoders with differential outputs
3. **Shielded enclosures**: Protect Pi and encoder from EMI

### Long Distance Connections

For connections >1 meter:

1. **RS-422 drivers**: Convert to differential signaling
2. **Twisted pair cable**: Cat5/Cat6 cable works well
3. **Termination**: Proper cable termination may be required

## Safety Considerations

### Electrical Safety

- **Never connect 5V signals directly to Pi GPIO pins**
- **Use proper fusing**: Protect against short circuits
- **ESD protection**: Use anti-static precautions

### Mechanical Safety

- **Secure mounting**: Prevent encoder from falling or moving
- **Shaft guards**: Protect rotating shafts from fingers/clothing
- **Emergency stops**: Include emergency stop functionality in applications

## Testing Hardware Setup

Use the provided test applications to verify your hardware:

```bash
# Basic functionality test
sudo ./test_rotary

# Multi-encoder test
sudo python3 examples/rotary_encoder.py multi

# Hardware debugging
sudo dmesg | grep rotary_encoder
```

Expected output for working hardware:
```
rotary_encoder: Encoder 0 initialized (CLK=18, DT=19, SW=20)
rotary_encoder: Driver initialized with 1 encoder(s)
```
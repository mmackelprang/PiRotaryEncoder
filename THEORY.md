# Theory of Operation

This document explains the theoretical principles behind rotary encoder operation and how the Raspberry Pi Rotary Encoder Device Driver implements quadrature decoding.

## Rotary Encoder Fundamentals

### What is a Rotary Encoder?

A rotary encoder is an electromechanical device that converts angular position or motion into digital signals. Unlike potentiometers which provide analog output, rotary encoders provide digital pulses that can be counted to determine position and direction.

### Types of Rotary Encoders

1. **Incremental Encoders** (used by this driver)
   - Provide relative position information
   - Generate pulses as the shaft rotates
   - No absolute position reference

2. **Absolute Encoders** (not covered by this driver)
   - Provide absolute position information
   - Each position has a unique digital code
   - More complex and expensive

## Quadrature Encoding

### Basic Principle

Incremental rotary encoders typically use quadrature encoding, which employs two output signals (Channel A and B, or CLK and DT) that are 90° out of phase. This phase relationship allows determination of both rotation direction and position.

### Signal Patterns

```
Clockwise Rotation:
CLK: ¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/
DT:  \_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\

Counter-Clockwise Rotation:
CLK: ¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/
DT:  /¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_/¯¯¯\_\_\_
```

### Phase Relationship

- **Clockwise**: CLK leads DT by 90°
- **Counter-clockwise**: DT leads CLK by 90°
- **Four states per cycle**: 00 → 01 → 11 → 10 → 00 (CW)
- **Full quadrature**: 4 transitions per encoder step

## Hardware Implementation

### Physical Construction

Most rotary encoders use one of these technologies:

1. **Optical Encoders**
   - LED light source
   - Photodetectors
   - Slotted or patterned disk
   - High resolution and reliability

2. **Magnetic Encoders**
   - Permanent magnets
   - Hall effect sensors
   - More robust in harsh environments

3. **Mechanical Encoders**
   - Metal contacts
   - Sliding brushes
   - Lower cost but limited lifespan

### Electrical Characteristics

#### Output Types

1. **Open Collector/Drain**
   - Requires external pull-up resistors
   - Common in industrial applications
   - Higher noise immunity

2. **Push-Pull (Totem Pole)**
   - Active high and low outputs
   - No external resistors needed
   - Common in hobbyist modules

#### Signal Levels

- **TTL**: 0V (low) to 5V (high)
- **CMOS**: 0V (low) to 3.3V (high) - Compatible with Raspberry Pi
- **Industrial**: Various levels (12V, 24V, etc.)

## Software Decoding Algorithms

### State Machine Approach

The driver implements a state machine to decode quadrature signals:

```
Previous State | Current State | Direction
    A  B       |    A  B       |
   ----|-------|-------|-------|----------
    0  0       |    0  1       | CW
    0  1       |    1  1       | CW  
    1  1       |    1  0       | CW
    1  0       |    0  0       | CW
   ----|-------|-------|-------|----------
    0  0       |    1  0       | CCW
    1  0       |    1  1       | CCW
    1  1       |    0  1       | CCW
    0  1       |    0  0       | CCW
```

### Edge Detection Methods

1. **Single Edge Detection** (used by this driver)
   - Monitor one channel (CLK) for transitions
   - Sample other channel (DT) on transition
   - Simple but half resolution

2. **Dual Edge Detection**
   - Monitor both channels for transitions
   - Full quadrature resolution
   - More complex interrupt handling

3. **Polling Method**
   - Regularly sample both channels
   - Software-intensive
   - May miss fast transitions

### Algorithm Implementation

The driver uses the following algorithm:

```c
void decode_quadrature(int clk_state, int dt_state, int last_clk) {
    // Only process on rising edge of CLK
    if (clk_state == 1 && last_clk == 0) {
        if (dt_state == 0) {
            // Clockwise: CLK rising while DT low
            position++;
            direction = 1;
        } else {
            // Counter-clockwise: CLK rising while DT high  
            position--;
            direction = -1;
        }
    }
}
```

## Interrupt-Driven Processing

### Why Use Interrupts?

1. **Real-time response**: Immediate processing of encoder events
2. **Efficient CPU usage**: CPU only active when needed
3. **No missed events**: Hardware queues interrupts
4. **Low latency**: Faster than polling methods

### Interrupt Configuration

The driver configures GPIO interrupts for:

- **CLK pin**: Rising and falling edge triggers
- **SW pin**: Falling edge trigger (button press)
- **DT pin**: Not directly monitored (sampled on CLK interrupt)

### Interrupt Service Routine (ISR)

```c
irqreturn_t rotary_clk_interrupt(int irq, void *dev_id) {
    // 1. Read current pin states
    // 2. Apply debouncing
    // 3. Decode direction
    // 4. Update position within range
    // 5. Wake up waiting processes
    // 6. Return from interrupt
}
```

## Debouncing Theory

### Why Debouncing is Needed

Mechanical contacts and optical sensors can produce multiple transitions during a single intended change due to:

- **Contact bounce**: Mechanical contacts physically bounce
- **Electrical noise**: EMI can cause false transitions
- **Switch settling time**: Time for signals to stabilize

### Debouncing Methods

1. **Hardware Debouncing**
   - RC circuits
   - Schmitt triggers
   - More complex but very effective

2. **Software Debouncing** (used by this driver)
   - Time-based filtering
   - Ignore transitions within debounce period
   - Configurable for different encoder types

### Debounce Algorithm

```c
unsigned long current_time = jiffies;
unsigned long debounce_period = msecs_to_jiffies(debounce_ms);

if (time_before(current_time, last_interrupt_time + debounce_period)) {
    return IRQ_HANDLED; // Ignore bounced signal
}

last_interrupt_time = current_time;
// Process the transition
```

## Range Management

### Position Limiting

The driver implements configurable position ranges:

```c
struct encoder_range {
    int min_value;    // Minimum allowed position
    int max_value;    // Maximum allowed position
    int current_pos;  // Current position
};

void update_position(struct encoder_range *range, int direction) {
    int new_pos = range->current_pos + direction;
    
    // Clamp to range
    if (new_pos < range->min_value) {
        new_pos = range->min_value;
    } else if (new_pos > range->max_value) {
        new_pos = range->max_value;
    }
    
    range->current_pos = new_pos;
}
```

### Range Applications

- **Volume Control**: 0 to 100
- **Menu Selection**: 0 to N-1 items
- **Temperature Setting**: -10°C to 50°C
- **Motor Position**: -180° to +180°

## Button Integration

### Switch Debouncing

Button switches require additional debouncing considerations:

1. **Press debouncing**: Ignore multiple presses in short time
2. **Release debouncing**: Handle proper button release
3. **Long press detection**: Could be added for advanced functionality

### Implementation

```c
irqreturn_t rotary_sw_interrupt(int irq, void *dev_id) {
    int sw_state = gpio_get_value(sw_gpio);
    
    // Button press on falling edge (pull-up assumed)
    if (sw_state == 0 && button_last_state == 1) {
        if (time_after(jiffies, button_last_time + debounce_time)) {
            button_pressed = 1;
            button_last_time = jiffies;
        }
    }
    
    button_last_state = sw_state;
}
```

## Performance Considerations

### Maximum Rotation Speed

The maximum rotation speed depends on:

1. **Encoder resolution**: Steps per revolution
2. **CPU interrupt latency**: Time to process interrupts  
3. **Debounce time**: Minimum time between valid transitions

**Example calculation:**
- Encoder: 24 steps/revolution
- Debounce: 10ms
- Max speed: 1/(0.01 × 24) = 4.17 revolutions/second

### Memory Usage

Per encoder data structure:
```c
struct rotary_encoder {
    // GPIO configuration: 3 × 4 bytes = 12 bytes
    // Position data: 4 × 4 bytes = 16 bytes  
    // Timing data: 3 × 8 bytes = 24 bytes
    // Synchronization: mutex + wait_queue ≈ 64 bytes
    // Total: ≈ 116 bytes per encoder
};
```

### CPU Impact

- **Interrupt overhead**: ~1-5μs per transition
- **Context switching**: Minimal for simple ISR
- **Wake-up latency**: <1ms for waiting processes

## Error Handling

### Common Error Conditions

1. **Missed transitions**: High-speed rotation exceeding processing capability
2. **Invalid states**: Impossible quadrature state transitions
3. **Hardware faults**: Stuck pins, broken connections
4. **Range violations**: Attempting to exceed configured limits

### Error Detection

```c
// Detect invalid state transitions
int valid_transitions[4][4] = {
    //  00  01  10  11
    {  1,  1,  1,  0 }, // From 00
    {  1,  1,  0,  1 }, // From 01  
    {  1,  0,  1,  1 }, // From 10
    {  0,  1,  1,  1 }  // From 11
};

if (!valid_transitions[last_state][current_state]) {
    // Invalid transition detected
    error_count++;
}
```

### Recovery Strategies

1. **State reset**: Reset to known good state
2. **Error counting**: Track error frequency
3. **Adaptive debouncing**: Increase debounce time on errors
4. **User notification**: Report persistent errors

## Advanced Topics

### Multi-X Decoding

Some applications require higher resolution:

- **1X decoding**: One count per step (driver default)
- **2X decoding**: Two counts per step (both edges of one channel)
- **4X decoding**: Four counts per step (all transitions)

### Velocity Calculation

Calculate rotation speed:

```c
unsigned long time_diff = current_time - last_time;
int position_diff = current_position - last_position;

if (time_diff > 0) {
    velocity = (position_diff * HZ) / time_diff; // Steps per second
}
```

### Index Channel Support

Some encoders provide a third "index" or "Z" channel:

- One pulse per revolution
- Used for absolute position reference
- Could be added to future driver versions

## References

1. "Incremental Encoder Interface Design" - Application Note
2. "Quadrature Decoder Implementation" - Technical Reference
3. "GPIO Interrupt Handling in Linux" - Kernel Documentation
4. "Real-Time Systems Design" - Academic Reference
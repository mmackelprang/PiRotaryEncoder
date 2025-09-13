# Rotary Encoder Configuration File Implementation

## Overview

This implementation adds configuration file support to the Raspberry Pi Rotary Encoder Driver, allowing users to configure multiple encoders through a simple text file instead of module parameters.

## Implementation Details

### Configuration File Format

Location: `/etc/rotary_encoder.conf` (configurable with `config_file` module parameter)

Format: `encoder_id=name:clk_pin:dt_pin:sw_pin:min_range:max_range`

Example:
```
0=volume_control:18:19:20:-100:100
1=menu_nav:22:23:-1:0:10
```

### Key Features

1. **Automatic Fallback**: If configuration file is missing, driver emits warning and uses default settings
2. **Input Validation**: Configuration parser validates GPIO pins (0-27) and ranges
3. **Encoder Names**: Each encoder can have a descriptive name shown in status output
4. **Flexible Configuration**: Only enabled encoders are initialized, saving resources
5. **Backwards Compatibility**: Module parameters still work as fallback

### Code Changes

#### Kernel Module (`rotary_encoder.c`)
- Added `struct encoder_config` for configuration storage
- Implemented `load_encoder_config()` function with file parsing
- Added encoder name field to `struct rotary_encoder`
- Updated initialization to use configuration data
- Enhanced status reporting with encoder names

#### Header File (`rotary_encoder.h`)
- Added `name[32]` field to `struct rotary_status`
- Structure size: 56 bytes (due to alignment)

#### Examples and Tests
- Updated Python interface in `examples/rotary_encoder.py`
- Updated C examples (`test_rotary.c`, `examples/volume_control.c`)
- Added configuration validation script (`test_config.py`)
- Added integration test suite (`test_integration.sh`)

#### Documentation
- Updated `README.md` with configuration file section
- Added module parameter documentation
- Updated API reference with new structure layout

#### Build System
- Added `install-config` target to Makefile
- Configuration file automatically installed as example

### API Impact

The rotary_status structure has been extended with a name field:

**Before:**
```c
struct rotary_status {
    int encoder_id;
    int position;
    int direction;
    int button_pressed;
    unsigned long timestamp;
}; // 20 bytes (32-bit) / 24 bytes (64-bit)
```

**After:**
```c
struct rotary_status {
    int encoder_id;
    int position;
    int direction;
    int button_pressed;
    unsigned long timestamp;
    char name[32];
}; // 56 bytes (with padding)
```

### Validation and Testing

All changes have been validated with:
- Configuration file parsing tests
- C compilation tests
- Python syntax validation
- Structure size verification
- Documentation consistency checks
- Integration testing

### Usage Examples

**Load with configuration file:**
```bash
sudo insmod rotary_encoder.ko
```

**Load with custom config file:**
```bash
sudo insmod rotary_encoder.ko config_file=/home/pi/encoders.conf
```

**Install configuration:**
```bash
make install-config
```

**Test configuration:**
```bash
python3 test_config.py /etc/rotary_encoder.conf
```

## Benefits

1. **User-Friendly**: Text file configuration is easier than module parameters
2. **Self-Documenting**: Names make encoder identification clear
3. **Flexible**: Different ranges and pin assignments per encoder
4. **Maintainable**: Configuration changes don't require module reload
5. **Professional**: Follows Linux driver configuration patterns
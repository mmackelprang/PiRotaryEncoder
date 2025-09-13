#!/usr/bin/env python3
"""
Test script for rotary encoder configuration file parsing.
This simulates the configuration parsing logic to validate the format.
"""

import re


def parse_config_line(line):
    """Parse a single configuration line"""
    # Skip empty lines and comments
    line = line.strip()
    if not line or line.startswith('#'):
        return None
    
    # Format: encoder_id=name:clk_pin:dt_pin:sw_pin:min_range:max_range
    pattern = r'^(\d+)=([^:]+):(\d+):(\d+):(-?\d+):(-?\d+):(-?\d+)$'
    match = re.match(pattern, line)
    
    if not match:
        raise ValueError(f"Invalid configuration line: {line}")
    
    encoder_id, name, clk_pin, dt_pin, sw_pin, min_range, max_range = match.groups()
    
    # Validate values
    encoder_id = int(encoder_id)
    clk_pin = int(clk_pin)
    dt_pin = int(dt_pin)
    sw_pin = int(sw_pin)
    min_range = int(min_range)
    max_range = int(max_range)
    
    if encoder_id < 0 or encoder_id >= 4:
        raise ValueError(f"Invalid encoder ID: {encoder_id}")
    if clk_pin < 0 or clk_pin > 27:
        raise ValueError(f"Invalid CLK pin: {clk_pin}")
    if dt_pin < 0 or dt_pin > 27:
        raise ValueError(f"Invalid DT pin: {dt_pin}")
    if sw_pin < -1 or sw_pin > 27:
        raise ValueError(f"Invalid SW pin: {sw_pin}")
    if min_range >= max_range:
        raise ValueError(f"Invalid range: {min_range} >= {max_range}")
    
    return {
        'encoder_id': encoder_id,
        'name': name,
        'clk_pin': clk_pin,
        'dt_pin': dt_pin,
        'sw_pin': sw_pin,
        'min_range': min_range,
        'max_range': max_range
    }


def test_config_file(filename):
    """Test configuration file parsing"""
    print(f"Testing configuration file: {filename}")
    
    encoders = {}
    
    try:
        with open(filename, 'r') as f:
            for line_num, line in enumerate(f, 1):
                try:
                    config = parse_config_line(line)
                    if config:
                        encoder_id = config['encoder_id']
                        if encoder_id in encoders:
                            print(f"Warning: Encoder {encoder_id} redefined at line {line_num}")
                        encoders[encoder_id] = config
                        print(f"Line {line_num}: OK - Encoder {encoder_id} ({config['name']})")
                except ValueError as e:
                    print(f"Line {line_num}: ERROR - {e}")
    
    except FileNotFoundError:
        print(f"ERROR: Configuration file '{filename}' not found")
        return False
    
    print(f"\nSummary:")
    print(f"  Valid encoders found: {len(encoders)}")
    for encoder_id, config in sorted(encoders.items()):
        print(f"    Encoder {encoder_id}: {config['name']} "
              f"(CLK={config['clk_pin']}, DT={config['dt_pin']}, SW={config['sw_pin']}, "
              f"range={config['min_range']}..{config['max_range']})")
    
    return True


if __name__ == "__main__":
    import sys
    
    # Test the example configuration file
    config_files = [
        "etc/rotary_encoder.conf",
        "rotary_encoder.conf"
    ]
    
    if len(sys.argv) > 1:
        config_files = sys.argv[1:]
    
    for config_file in config_files:
        test_config_file(config_file)
        print()
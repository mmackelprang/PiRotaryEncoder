#!/usr/bin/env python3
"""
Python Interface for Raspberry Pi Rotary Encoder Driver

This module provides a Python interface to the rotary encoder kernel driver,
making it easy to use rotary encoders in Python applications.

Author: Mark Mackelprang
License: MIT
"""

import fcntl
import struct
import select
import os
import signal
import sys

# IOCTL commands (must match kernel module)
ROTARY_IOC_MAGIC = ord('R')
ROTARY_SET_RANGE = (1 << 30) | (ROTARY_IOC_MAGIC << 8) | 1 | (12 << 16)
ROTARY_GET_POSITION = (2 << 30) | (ROTARY_IOC_MAGIC << 8) | 2 | (20 << 16)  
ROTARY_RESET = (1 << 30) | (ROTARY_IOC_MAGIC << 8) | 3 | (4 << 16)
ROTARY_SET_DEBOUNCE = (1 << 30) | (ROTARY_IOC_MAGIC << 8) | 4 | (4 << 16)

class RotaryEncoder:
    """Interface to the rotary encoder kernel driver"""
    
    def __init__(self, device_path="/dev/rotary_encoder"):
        """Initialize the rotary encoder interface
        
        Args:
            device_path: Path to the device file (default: /dev/rotary_encoder)
        """
        self.device_path = device_path
        self.fd = None
        self._open_device()
    
    def _open_device(self):
        """Open the device file"""
        try:
            self.fd = os.open(self.device_path, os.O_RDWR)
        except OSError as e:
            raise RuntimeError(f"Failed to open {self.device_path}: {e}")
    
    def close(self):
        """Close the device file"""
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
    
    def set_range(self, encoder_id, min_value, max_value):
        """Set the range for an encoder
        
        Args:
            encoder_id: Encoder ID (0-3)
            min_value: Minimum position value
            max_value: Maximum position value
        """
        if min_value >= max_value:
            raise ValueError("min_value must be less than max_value")
        
        range_data = struct.pack('iii', encoder_id, min_value, max_value)
        try:
            fcntl.ioctl(self.fd, ROTARY_SET_RANGE, range_data)
        except OSError as e:
            raise RuntimeError(f"Failed to set range: {e}")
    
    def get_position(self, encoder_id):
        """Get the current position of an encoder
        
        Args:
            encoder_id: Encoder ID (0-3)
            
        Returns:
            dict: {
                'encoder_id': int,
                'position': int,
                'direction': int,  # -1=CCW, 1=CW, 0=no change
                'button_pressed': bool,
                'timestamp': int
            }
        """
        status_data = struct.pack('iiiiL', encoder_id, 0, 0, 0, 0)
        try:
            result = fcntl.ioctl(self.fd, ROTARY_GET_POSITION, status_data)
            encoder_id, position, direction, button, timestamp = struct.unpack('iiiiL', result)
            return {
                'encoder_id': encoder_id,
                'position': position,
                'direction': direction,
                'button_pressed': bool(button),
                'timestamp': timestamp
            }
        except OSError as e:
            raise RuntimeError(f"Failed to get position: {e}")
    
    def reset(self, encoder_id):
        """Reset encoder position to 0
        
        Args:
            encoder_id: Encoder ID (0-3)
        """
        encoder_data = struct.pack('i', encoder_id)
        try:
            fcntl.ioctl(self.fd, ROTARY_RESET, encoder_data)
        except OSError as e:
            raise RuntimeError(f"Failed to reset encoder: {e}")
    
    def set_debounce(self, debounce_ms):
        """Set debounce time for all encoders
        
        Args:
            debounce_ms: Debounce time in milliseconds (0-100)
        """
        if not 0 <= debounce_ms <= 100:
            raise ValueError("Debounce time must be between 0 and 100 ms")
        
        debounce_data = struct.pack('i', debounce_ms)
        try:
            fcntl.ioctl(self.fd, ROTARY_SET_DEBOUNCE, debounce_data)
        except OSError as e:
            raise RuntimeError(f"Failed to set debounce: {e}")
    
    def read_events(self, timeout=None):
        """Read encoder events (blocking or with timeout)
        
        Args:
            timeout: Timeout in seconds (None for blocking)
            
        Returns:
            list: List of event dictionaries for all encoders
        """
        ready, _, _ = select.select([self.fd], [], [], timeout)
        
        if not ready:
            return []  # Timeout
        
        try:
            # Read data for all encoders (up to 4 * 20 bytes)
            data = os.read(self.fd, 80)
            events = []
            
            # Parse events (each event is 20 bytes)
            for i in range(0, len(data), 20):
                if i + 20 <= len(data):
                    encoder_id, position, direction, button, timestamp = struct.unpack('iiiiL', data[i:i+20])
                    events.append({
                        'encoder_id': encoder_id,
                        'position': position,
                        'direction': direction,
                        'button_pressed': bool(button),
                        'timestamp': timestamp
                    })
            
            return events
        except OSError as e:
            raise RuntimeError(f"Failed to read events: {e}")

# Example usage and demo application
def demo_volume_control():
    """Demo application: Volume control with rotary encoder"""
    print("Python Rotary Encoder Demo - Volume Control")
    print("Turn encoder to adjust volume (0-100%)")
    print("Press button to mute/unmute")
    print("Press Ctrl+C to exit\n")
    
    running = True
    muted = False
    volume = 50
    
    def signal_handler(sig, frame):
        nonlocal running
        running = False
        print("\nShutting down...")
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        with RotaryEncoder() as encoder:
            # Setup encoder 0 for volume control (0-100)
            encoder.set_range(0, 0, 100)
            encoder.reset(0)
            encoder.set_debounce(10)  # 10ms debounce
            
            print(f"Initial volume: {volume}%")
            
            while running:
                events = encoder.read_events(timeout=1.0)
                
                for event in events:
                    if event['encoder_id'] == 0:  # Only process encoder 0
                        if event['direction'] != 0:
                            # Volume changed
                            volume = event['position']
                            status = " (MUTED)" if muted else ""
                            print(f"Volume: {volume}%{status}")
                            
                            # Here you could use subprocess to call amixer:
                            # subprocess.run(['amixer', 'set', 'Master', f'{volume}%'], 
                            #                capture_output=True)
                        
                        if event['button_pressed']:
                            # Toggle mute
                            muted = not muted
                            status = "MUTED" if muted else "UNMUTED"
                            print(f"{status}")
                            
                            # Here you could use subprocess to call amixer:
                            # cmd = ['amixer', 'set', 'Master', 'mute' if muted else 'unmute']
                            # subprocess.run(cmd, capture_output=True)
    
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    print("Demo terminated.")
    return 0

def demo_multi_encoder():
    """Demo application: Monitor multiple encoders"""
    print("Python Rotary Encoder Demo - Multi-Encoder Monitor")
    print("This demo monitors all available encoders")
    print("Press Ctrl+C to exit\n")
    
    running = True
    
    def signal_handler(sig, frame):
        nonlocal running
        running = False
        print("\nShutting down...")
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        with RotaryEncoder() as encoder:
            # Setup different ranges for different encoders
            ranges = [
                (0, -100, 100),   # Encoder 0: -100 to 100
                (1, 0, 255),      # Encoder 1: 0 to 255
                (2, -50, 50),     # Encoder 2: -50 to 50
                (3, 0, 1000),     # Encoder 3: 0 to 1000
            ]
            
            for enc_id, min_val, max_val in ranges:
                try:
                    encoder.set_range(enc_id, min_val, max_val)
                    encoder.reset(enc_id)
                    print(f"Encoder {enc_id}: range {min_val} to {max_val}")
                except:
                    print(f"Encoder {enc_id}: not available")
            
            print()
            
            while running:
                events = encoder.read_events(timeout=1.0)
                
                for event in events:
                    if event['direction'] != 0 or event['button_pressed']:
                        enc_id = event['encoder_id']
                        pos = event['position']
                        direction = "CW" if event['direction'] > 0 else "CCW" if event['direction'] < 0 else ""
                        button = " [BUTTON]" if event['button_pressed'] else ""
                        
                        print(f"Encoder {enc_id}: {pos:4d} {direction:3s}{button}")
    
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    print("Demo terminated.")
    return 0

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "multi":
        sys.exit(demo_multi_encoder())
    else:
        sys.exit(demo_volume_control())
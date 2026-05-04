# ESP32-S3 AI CAM - Automation Agent v1.0.0
## Serial API Interface - Phase 1 Documentation

---

## Overview

The Automation Agent adds a serial command interface to the ESP32-S3 AI CAM, allowing external devices (Arduino UNO, Raspberry Pi, etc.) to control camera capture, audio recording, and access stored data programmatically.

**All existing web interface features remain fully functional.**

---

## Quick Start

### 1. Upload the Firmware
Upload `open_ai_automation_agent_1_0_0.ino` to your ESP32-S3 AI CAM

### 2. Open Serial Monitor
- Baud Rate: **115200**
- Line Ending: **Newline** or **Both NL & CR**

### 3. Test Connection
Type: `CMD:PING`

Expected Response: `OK:PONG`

---

## Serial Protocol Specification

### Command Format
```
CMD:<command>|PARAM:<value>\n
```

**Examples:**
```
CMD:PING\n
CMD:CAPTURE\n
CMD:RECORD:5\n
CMD:STATUS\n
```

### Response Format

**Success:**
```
OK:<data>\n
```

**Error:**
```
ERROR:<message>\n
```

**Debug Echo (optional):**
```
# Processing: <command>\n
```

---

## Available Commands (Phase 1)

### 1. PING
**Description:** Test connection and verify ESP32 is responding

**Command:**
```
CMD:PING
```

**Response:**
```
OK:PONG
```

**Use Case:** Connection testing, heartbeat monitoring

---

### 2. VERSION
**Description:** Get firmware version

**Command:**
```
CMD:VERSION
```

**Response:**
```
OK:AUTOMATION_AGENT_1.0.0
```

**Use Case:** Verify firmware version, compatibility checking

---

### 3. STATUS
**Description:** Get comprehensive system status

**Command:**
```
CMD:STATUS
```

**Response:**
```
OK:READY|HEAP:215000|IMAGES:5|AUDIO:3|STREAMING:ON
```

**Response Fields:**
- `READY` - System state (READY, RECORDING, ANALYZING)
- `HEAP` - Free heap memory in bytes
- `IMAGES` - Number of images stored
- `AUDIO` - Number of audio files stored
- `STREAMING` - Camera streaming status (ON/OFF)

**Use Case:** Health monitoring, resource checking before operations

---

### 4. CAPTURE
**Description:** Capture a photo from the camera

**Command:**
```
CMD:CAPTURE
```

**Response (Success):**
```
OK:IMG_5.jpg
```

**Response (Error):**
```
ERROR:Camera capture failed
ERROR:Streaming not enabled
ERROR:SD card write failed
```

**Notes:**
- Saves image to `/images/` directory on SD card
- Automatically increments filename counter
- Camera streaming must be enabled
- Returns filename on success

**Use Case:** Automated image capture, time-lapse, event-triggered photography

---

### 5. RECORD
**Description:** Record audio for specified duration

**Command:**
```
CMD:RECORD:5
```
*Records 5 seconds of audio*

**Parameters:**
- Duration: 1-60 seconds

**Response (Success):**
```
OK:REC_3.wav
```

**Response (Error):**
```
ERROR:System busy
ERROR:Duration must be 1-60 seconds
ERROR:Memory allocation failed
ERROR:SD card write failed
```

**Notes:**
- Saves audio to `/audio/` directory as 16kHz WAV
- Blocks during recording (5 seconds = ~5 second delay)
- Cannot record while analyzing or playing TTS
- Automatically increments filename counter

**Use Case:** Voice commands, audio logging, event recording

---

### 6. LIST_IMAGES
**Description:** Get list of all stored images

**Command:**
```
CMD:LIST_IMAGES
```

**Response (Files exist):**
```
OK:IMG_1.jpg,IMG_2.jpg,IMG_3.jpg,IMG_4.jpg,IMG_5.jpg
```

**Response (No files):**
```
OK:NONE
```

**Response (Error):**
```
ERROR:Cannot open images directory
```

**Notes:**
- Returns comma-separated list of filenames
- Files are in `/images/` directory
- Use for file inventory, selection

**Use Case:** File management, checking available images before operations

---

### 7. LIST_AUDIO
**Description:** Get list of all stored audio files

**Command:**
```
CMD:LIST_AUDIO
```

**Response (Files exist):**
```
OK:REC_1.wav,REC_2.wav,REC_3.wav
```

**Response (No files):**
```
OK:NONE
```

**Response (Error):**
```
ERROR:Cannot open audio directory
```

**Notes:**
- Returns comma-separated list of filenames
- Files are in `/audio/` directory

**Use Case:** File management, checking available audio before operations

---

### 8. GET_LAST_IMAGE
**Description:** Get filename of most recently captured image

**Command:**
```
CMD:GET_LAST_IMAGE
```

**Response (Success):**
```
OK:IMG_5.jpg
```

**Response (Error):**
```
ERROR:No image captured yet
```

**Notes:**
- Only tracks images captured during current session
- Resets on ESP32 reboot
- Useful after CAPTURE command

**Use Case:** Retrieving result of capture operation

---

### 9. GET_LAST_AUDIO
**Description:** Get filename of most recently recorded audio

**Command:**
```
CMD:GET_LAST_AUDIO
```

**Response (Success):**
```
OK:REC_3.wav
```

**Response (Error):**
```
ERROR:No audio recorded yet
```

**Notes:**
- Only tracks audio recorded during current session
- Resets on ESP32 reboot
- Useful after RECORD command

**Use Case:** Retrieving result of record operation

---

## Error Handling

### Common Error Messages

| Error | Meaning | Solution |
|-------|---------|----------|
| `Invalid command format` | Command not formatted correctly | Use `CMD:command` format |
| `Unknown command: X` | Command not recognized | Check spelling, see command list |
| `System busy` | Operation in progress | Wait and retry |
| `Streaming not enabled` | Camera not active | Enable streaming first |
| `SD card write failed` | Storage issue | Check SD card |
| `Memory allocation failed` | Not enough RAM | Reduce recording duration |
| `Command too long` | Command exceeds 256 chars | Shorten command |

---

## Testing Procedures

### Method 1: Arduino Serial Monitor (Easiest)

1. Upload firmware to ESP32
2. Open Arduino IDE Serial Monitor
3. Set baud rate to **115200**
4. Set line ending to **Newline**
5. Type commands and press Enter

**Test Sequence:**
```
CMD:PING
CMD:VERSION
CMD:STATUS
CMD:CAPTURE
CMD:GET_LAST_IMAGE
CMD:RECORD:3
CMD:GET_LAST_AUDIO
CMD:LIST_IMAGES
CMD:LIST_AUDIO
```

---

### Method 2: Arduino UNO Test Sketch

1. Upload `ESP32_Serial_Test.ino` to Arduino UNO (or just use as reference)
2. Open Serial Monitor at 115200 baud
3. Automated test sequence runs
4. View results and interact

**Hardware Connections:**
```
Arduino UNO          ESP32-S3
-----------          --------
TX (Pin 1)    →      RX (GPIO pin - check your board)
RX (Pin 0)    →      TX (GPIO pin - check your board)
GND           →      GND
```

**Note:** For initial testing, just use ESP32's own Serial Monitor!

---

### Method 3: Python Script (Advanced)

```python
import serial
import time

# Open serial connection
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=5)
time.sleep(2)  # Wait for ESP32 to boot

def send_command(cmd):
    print(f">>> {cmd}")
    ser.write(f"{cmd}\n".encode())
    response = ser.readline().decode().strip()
    print(f"<<< {response}\n")
    return response

# Test sequence
send_command("CMD:PING")
send_command("CMD:VERSION")
send_command("CMD:STATUS")
send_command("CMD:CAPTURE")
send_command("CMD:LIST_IMAGES")

ser.close()
```

---

## Example Use Cases

### 1. Automated Time-Lapse
```python
# Take photo every 5 minutes
while True:
    send_command("CMD:CAPTURE")
    response = send_command("CMD:GET_LAST_IMAGE")
    print(f"Captured: {response}")
    time.sleep(300)  # 5 minutes
```

### 2. Voice-Activated Camera
```cpp
// Arduino with button
if (buttonPressed()) {
  Serial.println("CMD:RECORD:5");
  delay(5500);
  Serial.println("CMD:CAPTURE");
  Serial.println("CMD:GET_LAST_IMAGE");
}
```

### 3. Status Dashboard
```python
# Monitor system every minute
while True:
    status = send_command("CMD:STATUS")
    parse_and_display(status)
    time.sleep(60)
```

---

## Troubleshooting

### No Response to Commands

**Check:**
- Baud rate is 115200
- Line ending includes newline (\n)
- ESP32 fully booted (wait 5 seconds after reset)
- Correct serial port selected
- USB cable properly connected

### "System busy" Errors

**Cause:** Another operation in progress (recording, analyzing, TTS playback)

**Solution:** Wait for operation to complete, check with `CMD:STATUS`

### Garbled Output

**Check:**
- Baud rate matches (115200)
- No other devices on serial line
- Good quality USB cable
- Stable power supply

---

## Phase 1 Limitations

**Not Yet Implemented:**
- OpenAI analysis triggering via serial
- Custom prompt setting
- Binary data transfer (images/audio)
- File deletion commands
- Multi-command queuing
- Event notifications

**Coming in Phase 2+:**
- Full OpenAI workflow automation
- Large data transfers
- Advanced file management
- Event-driven notifications
- Arduino library wrapper

---

## Technical Details

### Serial Configuration
- **Baud Rate:** 115200
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1
- **Flow Control:** None

### Buffer Limits
- **Command Buffer:** 256 characters
- **Response Buffer:** Unlimited (streamed)

### Timing
- **Command Processing:** Non-blocking
- **CAPTURE:** ~100ms
- **RECORD:** Duration + 100ms
- **LIST commands:** ~50ms
- **STATUS:** ~100ms

### Memory Usage
- **Idle:** < 100 bytes
- **Recording 5s:** ~160KB (temporary)

---

## Support & Next Steps

### Current Version: 1.0.0 (Phase 1)
- Basic command interface
- Camera capture
- Audio recording
- File listing
- Status monitoring

### Planned Features (Future Phases)
- Phase 2: Full OpenAI integration via serial
- Phase 3: Binary data transfer protocol
- Phase 4: Arduino library wrapper
- Phase 5: Advanced automation features

---

## Quick Reference Card

```
┌─────────────────────────────────────────────────────────┐
│  ESP32-S3 AI CAM - Serial Commands (Phase 1)          │
├─────────────────────────────────────────────────────────┤
│  CMD:PING              → Test connection               │
│  CMD:VERSION           → Get firmware version          │
│  CMD:STATUS            → System status                 │
│  CMD:CAPTURE           → Take photo                    │
│  CMD:RECORD:5          → Record 5 sec audio            │
│  CMD:LIST_IMAGES       → List images                   │
│  CMD:LIST_AUDIO        → List audio files              │
│  CMD:GET_LAST_IMAGE    → Last captured image           │
│  CMD:GET_LAST_AUDIO    → Last recorded audio           │
├─────────────────────────────────────────────────────────┤
│  Baud: 115200  |  Format: CMD:command\n              │
│  Response: OK:data or ERROR:message                    │
└─────────────────────────────────────────────────────────┘
```

---

**Happy Automating! 🤖📷🎤**

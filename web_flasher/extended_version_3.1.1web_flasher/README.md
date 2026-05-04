# Firmware Binary Files

This folder should contain the 4 binary files needed for flashing:

```
firmware/
├── bootloader.bin      (from Arduino IDE export)
├── partitions.bin      (from Arduino IDE export)
├── boot_app0.bin       (from Arduino IDE export)
└── firmware.bin        (from Arduino IDE export)
```

---

## 📝 How to Generate These Files

### Step 1: Open in Arduino IDE
Open `ESP32_AI_Camera_v3_0_0.ino` with all header files in the same folder.

### Step 2: Configure Board
```
Tools → Board → ESP32S3 Dev Module
Tools → Flash Size → 16MB (128Mb)
Tools → Partition Scheme → 16M Flash (3MB APP/9.9MB FATFS)
Tools → PSRAM → OPI PSRAM
```

### Step 3: Export Binary
```
Sketch → Export Compiled Binary (Ctrl+Alt+S)
```

### Step 4: Find Binary Files
Look in your sketch folder's `build` subdirectory:
- Windows: `Documents\Arduino\ESP32_AI_Camera_v3_0_0\build\`
- Mac: `~/Documents/Arduino/ESP32_AI_Camera_v3_0_0/build/`
- Linux: `~/Arduino/ESP32_AI_Camera_v3_0_0/build/`

### Step 5: Copy & Rename
Copy these files to this `firmware/` folder:

1. `ESP32_AI_Camera_v3_0_0.ino.bootloader.bin` → `bootloader.bin`
2. `ESP32_AI_Camera_v3_0_0.ino.partitions.bin` → `partitions.bin`
3. `boot_app0.bin` → `boot_app0.bin` (from Arduino15 packages folder)
4. `ESP32_AI_Camera_v3_0_0.ino.bin` → `firmware.bin`

**boot_app0.bin location:**
```
Windows: C:\Users\[User]\AppData\Local\Arduino15\packages\esp32\hardware\esp32\[version]\tools\partitions\boot_app0.bin
Mac: ~/Library/Arduino15/packages/esp32/hardware/esp32/[version]/tools/partitions/boot_app0.bin
Linux: ~/.arduino15/packages/esp32/hardware/esp32/[version]/tools/partitions/boot_app0.bin
```

---

## ✅ Verification

After copying, this folder should contain exactly 4 .bin files:
```
firmware/
├── bootloader.bin    (~26 KB)
├── partitions.bin    (~3 KB)
├── boot_app0.bin     (~4 KB)
└── firmware.bin      (~1.3 MB)
```

Total size: ~1.33 MB

---

## 🚀 Next Steps

Once you have all 4 binary files:
1. Test locally: `python -m http.server 8000`
2. Open: `http://localhost:8000`
3. Try flashing to verify it works
4. Host on GitHub Pages or Netlify
5. Share the URL!

---

*See BUILD_GUIDE.md for complete instructions*

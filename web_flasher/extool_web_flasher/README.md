# 🌐 ESP32-S3 AI Camera Web Flasher

**Install firmware from your browser - No Arduino IDE required!**

---

## 🎯 What Is This?

A web-based firmware installer that lets anyone flash the ESP32-S3 AI Camera firmware directly from their browser with a single click. Perfect for:
- End users who don't want to install Arduino IDE
- Quick firmware updates
- Mass deployment scenarios
- Non-technical users

---

## ⚡ Quick Start

### For Users (Installing Firmware):

1. **Open the web flasher** (wherever you hosted it)
2. **Connect your ESP32-S3** via USB-C cable
3. **Click "Install AI Camera Firmware"**
4. **Select your device** from the list
5. **Wait 2-3 minutes** for flashing
6. **Done!** Device will reboot and create setup WiFi

**That's it!** No downloads, no IDE, no configuration.

---

### For Developers (Building the Package):

1. **Compile firmware** in Arduino IDE
2. **Export compiled binary** (Sketch → Export)
3. **Copy 4 binary files** to `firmware/` folder:
   - `bootloader.bin`
   - `partitions.bin`
   - `boot_app0.bin`
   - `firmware.bin`
4. **Host the package** (GitHub Pages, Netlify, or your server)
5. **Share the URL!**

Full instructions in **BUILD_GUIDE.md**

---

## 📁 Package Structure

```
web_flasher_package/
├── index.html          ← Web installer interface
├── manifest.json       ← Flash configuration
├── BUILD_GUIDE.md      ← Detailed build instructions
├── README.md           ← This file
└── firmware/           ← Binary files (create these)
    ├── bootloader.bin
    ├── partitions.bin
    ├── boot_app0.bin
    └── firmware.bin
```

---

## 🔧 Requirements

### User Requirements:
- **Browser:** Chrome, Edge, or Opera (WebSerial support)
- **Hardware:** DFRobot ESP32-S3 AI CAM (DFR1154)
- **Cable:** USB-C data cable (not charge-only)

### Developer Requirements:
- **Arduino IDE** with ESP32-S3 support
- **Web hosting** (GitHub Pages, Netlify, or any HTTPS server)
- **Firmware source** (included in parent package)

---

## 🚀 Hosting Options

### Option 1: GitHub Pages (Recommended)
```bash
1. Create GitHub repo
2. Upload all files
3. Enable Pages in settings
4. URL: https://[username].github.io/[repo]/
```

### Option 2: Netlify
```bash
1. Sign up at netlify.com
2. Drag & drop the folder
3. Get instant URL
```

### Option 3: Local Testing
```bash
python -m http.server 8000
# Open: http://localhost:8000
```

---

## 🎓 How It Works

1. **ESP Web Tools** library handles WebSerial communication
2. **manifest.json** defines which files to flash and where
3. **Browser** downloads binaries and flashes via WebSerial
4. **No middleware** - direct browser-to-device communication

---

## ✨ Features

- ✅ **One-click installation** - No technical knowledge required
- ✅ **Cross-platform** - Works on Windows, Mac, Linux, ChromeOS
- ✅ **No drivers needed** - Browser handles everything
- ✅ **Progress tracking** - Real-time flash progress
- ✅ **Responsive design** - Works on desktop and mobile
- ✅ **Professional UI** - Beautiful gradient design

---

## 📊 Technical Details

### Binary Locations:
```
0x0000    bootloader.bin   (26 KB)
0x8000    partitions.bin   (3 KB)
0xE000    boot_app0.bin    (4 KB)
0x10000   firmware.bin     (1.3 MB)
```

### Supported Chips:
- ESP32-S3 ✅
- Can be adapted for ESP32, ESP32-C3, ESP32-S2

---

## 🐛 Troubleshooting

**Install button doesn't work:**
- Use Chrome, Edge, or Opera (Firefox/Safari not supported yet)
- Make sure site is HTTPS or localhost

**No device found:**
- Check USB cable is for data (not charge-only)
- Try different USB port
- Install CH340/CP2102 drivers if needed

**Flash fails:**
- Put device in download mode (hold BOOT, press RESET)
- Try different USB cable
- Check Device Manager (Windows) for COM port

---

## 📝 Customization

### Change Colors:
Edit CSS in `index.html`:
```css
background: linear-gradient(135deg, #YOUR_COLOR1, #YOUR_COLOR2);
```

### Add Branding:
Add your logo in the header section:
```html
<img src="your-logo.png" alt="Logo">
```

### Modify Text:
All text is in `index.html` - easy to customize!

---

## 🎯 Use Cases

### 1. Product Distribution
Ship devices without firmware, let customers flash via web.

### 2. Firmware Updates
Share update link instead of sending files.

### 3. Workshops/Education
Students can flash without IDE installation.

### 4. Beta Testing
Easy distribution of beta firmware to testers.

### 5. Multi-Device Setup
Flash multiple devices quickly without re-compiling.

---

## 📞 Support

For issues with:
- **Web flasher itself:** Check BUILD_GUIDE.md
- **Firmware functionality:** See main firmware docs
- **ESP Web Tools:** Visit https://esphome.github.io/esp-web-tools/

---

## 🎉 Benefits

| Traditional Method | Web Flasher |
|-------------------|-------------|
| Install Arduino IDE (500MB+) | Just open browser |
| Configure board settings | Click one button |
| Find correct port | Auto-detected |
| Wait for compile | Pre-compiled binary |
| 20+ minute setup | 2 minute flash |

---

## 📄 License

Same as main firmware package (see parent directory).

---

## 🙏 Credits

- **ESP Web Tools:** ESPHome team
- **WebSerial API:** W3C/Google
- **Firmware:** ESP32-S3 AI Camera v3.0.0

---

## 🔗 Links

- **ESP Web Tools:** https://esphome.github.io/esp-web-tools/
- **WebSerial:** https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API
- **ESP32-S3 Docs:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/

---

**Ready to flash?** Build your binaries and host the package!

See **BUILD_GUIDE.md** for complete instructions.

---

*ESP32-S3 AI Camera Web Flasher v3.0.0*  
*Making firmware installation accessible to everyone* 🚀

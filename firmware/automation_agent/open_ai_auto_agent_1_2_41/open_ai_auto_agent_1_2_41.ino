#include "time.h"
/*
 * ESP32-S3 AI CAM - OpenAI Automation Agent v1.2.41
 *
 * BOARD: ESP32S3 Dev Module  (esp32:esp32:esp32s3)
 *
 * TOOLS MENU SETTINGS (Arduino IDE 2.x):
 * -------------------------------------------------------
 * Board                    : ESP32S3 Dev Module
 * USB CDC On Boot          : Enabled          (CDCOnBoot=cdc)
 * CPU Frequency            : 240MHz (WiFi)    (CPUFreq=240)
 * Flash Mode               : QIO 80MHz        (FlashMode=qio)
 * Flash Size               : 16MB (128Mb)     (FlashSize=16M)
 * Partition Scheme         : 16M Flash (3MB APP/9.9MB FATFS)
 *                            (PartitionScheme=app3M_fat9M_16MB)
 * PSRAM                    : OPI PSRAM        (PSRAM=opi)
 * Upload Speed             : 921600
 * Core Debug Level         : None
 * USB DFU On Boot          : Disabled
 * USB MSC On Boot          : Disabled
 * Erase All Flash on Upload: Disabled
 * -------------------------------------------------------
 * NOTE: A sketch.yaml file is included in this sketch folder.
 * Arduino IDE 2.x reads it automatically and pre-configures
 * the Tools menu -- no manual setup required after first open.
 *
 * arduino-cli FQBN (command-line builds):
 *   esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,
 *   PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,
 *   CPUFreq=240,FlashMode=qio,UploadSpeed=921600,DebugLevel=none
 * -------------------------------------------------------
 *
 * v1.2.41 FIX: UNO SERIAL LINK DOES NOT AUTO-START ON ESP32 BOOT
 * =================================================
 * Root cause: boot timing mismatch between the UNO and the ESP32.
 *
 * The UNO boots in approximately 2 seconds and completes its startup
 * sequence (including espSerial.begin()) well before the ESP32 is ready.
 * The ESP32 setup() takes 20-30 seconds (SD, camera, PDM mic, WiFi
 * connect, web server, TTS announcement). By the time the ESP32 enters
 * loop(), the UNO is already sitting idle and has nothing to trigger a
 * re-sync. The first heartbeat (HB|) was not emitted until 10 seconds
 * after loop() started -- meaning 30-40 seconds after power-on -- which
 * is too long and too infrequent to reliably re-sync the UNO.
 *
 * Result: if the ESP32 was powered on, reflashed, or reset AFTER the
 * UNO was already running, the serial link stayed silent until the UNO
 * was manually reset.
 *
 * Fix:
 *   1. ONLINE announcement: at the very end of setup(), after WiFi
 *      connects and everything is ready, the ESP32 immediately sends:
 *        ONLINE|VER:<version>|IP:<ip>
 *      This gives the UNO a specific event to listen for and act on
 *      (e.g. re-run its handshake or set a "connected" flag).
 *
 *   2. Repeated ONLINE: loop() re-sends the ONLINE message every
 *      UNO_SYNC_INTERVAL_MS (3 seconds) for the first
 *      UNO_SYNC_DURATION_MS (30 seconds) after boot. This covers the
 *      case where the UNO itself boots slightly after the ESP32, or
 *      where the first ONLINE message was missed due to UART buffering.
 *      After 30 seconds the repeating stops -- normal HB takes over.
 *
 *   3. Heartbeat interval reduced from 10 seconds to 5 seconds so the
 *      UNO has more frequent keepalive events to detect link health.
 *
 * UNO sketch update required: add handling for the ONLINE message in
 * the UNO's serial receive loop. On receiving a line starting with
 * "ONLINE|", the UNO should reset its connection state and re-send
 * any pending CMD:PING or status request to confirm the link.
 *
 * v1.2.40 FIX: REMOTE PANEL / SERIAL PANEL WIDTH EXCEEDS MAIN GRID
 * =================================================
 * Root cause: .header, .remote-status, .remote-panel, and .serial-panel
 * all sat outside .main-grid in the HTML, so they had no width constraint.
 * .main-grid had max-width:1400px; margin:0 auto which kept the content
 * area centred and bounded, but the panels above it stretched to full
 * viewport width (body padding only), making the Remote Control panel
 * visibly wider than the main content area on large monitors.
 *
 * Fix: introduced a .page-wrap div (max-width:1400px; margin:0 auto)
 * that wraps ALL page content -- header, remote-status bar, remote panel,
 * serial panel, main-grid, and the diagnostics overlay. The max-width
 * constraint is removed from .main-grid (it now inherits from .page-wrap).
 * All panels now share the same maximum width and centre together.
 *
 * Also fixed: a stray comment-close marker that ended the header block early
 * (after the v1.2.38 entry), leaving all older changelog entries as bare
 * uncommented text. Removed the stray close-marker and orphaned line.
 *
 * v1.2.39 FIX: REMOTE CONTROL PIN PANEL BUTTONS NEVER UPDATED
 * =================================================
 * Root cause: all PIN handlers (handlePinTakePic, handlePinNextImage, etc.)
 * execute synchronously inside processSerialCommand(). They call
 * remoteCommandBegin() at the start and remoteCommandEnd() at the end,
 * but both calls happen BEFORE loop() ever returns to server.handleClient().
 * By the time the browser polls /remote/status (every 800ms), the C++ state
 * machine has already completed the full begin->execute->end cycle.
 * remoteActiveCommand is "" and remoteCommandDone was cleared on the same
 * poll it was read, so the browser saw neither the active state nor the done
 * flash -- all buttons stayed gray permanently.
 *
 * Fix -- C++ side (handleRemoteStatus / remoteCommandBegin / remoteCommandEnd):
 *   - remoteCommandBegin() now records remoteLastCommand and remoteActiveMs
 *     (millis() timestamp) in addition to setting remoteActiveCommand.
 *   - remoteCommandEnd() records remoteDoneMs instead of a bare bool flag.
 *   - handleRemoteStatus() reports activeCmd as non-empty for REMOTE_LATCH_MS
 *     (2000ms) after End is called, so fast commands remain visible across
 *     at least 2 poll cycles. cmdDone=true is also held for REMOTE_LATCH_MS.
 *   - The latch resets automatically once the done window expires.
 *   - remoteCommandDone (bare bool) removed; replaced by remoteDoneMs timestamp.
 *
 * Fix -- JS side (applyButtonState):
 *   - Simplified: server now owns all timing, JS just mirrors what it receives.
 *   - If activeCmd is set and cmdDone=false -> pulse yellow (remote-active).
 *   - If activeCmd is set and cmdDone=true  -> show green/red (done colour).
 *   - If activeCmd clears -> remove all highlight classes.
 *   - lastActiveCmd tracking removed (was the source of the miss-detection).
 *   - REMOTE_LATCH_MS = 2000ms guarantees 2+ poll cycles of visibility.
 *
 * v1.2.38 FIX: IMAGE FILE CLICK -> DISPLAY IN VIDEO AREA + REMOVE PIN LABELS
 * =================================================
 * Two UI corrections:
 *
 * 1. IMAGE FILE CLICK NOW DISPLAYS IMAGE IN VIDEO AREA (Review mode fix):
 *    Previously clicking an image file in the Image Files (Resources) panel
 *    only selected the file for export -- it did NOT display the image in the
 *    video display area. This made the "Review" workflow confusing because
 *    there was no visual confirmation of which image was selected.
 *
 *    Fix: selectImageFile() now also:
 *      - Stops the live stream (stopStreamRefresh(), streamActive=false)
 *      - Resets the stream button state to "Video Stream"
 *      - Loads the selected image into #videoStream via /image?file=<fn>
 *      - Shows "Viewing: <filename>" in the status bar instead of "Selected:"
 *    The image is immediately visible in the video panel upon click, matching
 *    the behavior of the Review button. The file remains selected for export,
 *    so Add to Export still works normally after viewing.
 *
 * 2. PIN DESCRIPTION LABELS REMOVED FROM REMOTE CONTROL PANEL:
 *    The <span class="pin-label">Pin X</span> labels inside each .rem-btn
 *    element have been removed. These small sub-labels (Pin 4, Pin 5, etc.)
 *    cluttered the remote control button display. The buttons now show only
 *    their action names (Take Pic, Del Pic, Record, etc.).
 *    The .rem-btn .pin-label CSS rule has also been removed.
 *
 * v1.2.37 FIX: TTS SPEAKER PLAYBACK SAMPLE RATE MISMATCH (align with v3.2.5)
 * =================================================
 * Root cause: handleTTSSpeaker() called playWAVSpeaker() which fed the 24kHz
 * TTS WAV through i2s_spk running at SAMPLE_RATE (16kHz). The result was
 * playback at 67% speed (16000/24000), lower pitch, and DMA buffer noise --
 * identical to the bug fixed in the standalone firmware at v3.2.5.
 *
 * Fix:
 *   - Added #define TTS_SAMPLE_RATE 24000 alongside SAMPLE_RATE.
 *   - Replaced handleTTSSpeaker() stub (which delegated to playWAVSpeaker)
 *     with a full inline implementation that reinitializes i2s_spk at
 *     TTS_SAMPLE_RATE (24kHz) before playback and restores it to SAMPLE_RATE
 *     (16kHz) afterward. Pattern is identical to v3.2.5 handleTTSSpeakerPlayback().
 *   - Agent-specific features retained in the new implementation:
 *       TTS_PLAY_ATTENUATION applied to prevent MAX98357A clipping.
 *       Fade-out on final chunk for clean mute boundary.
 *       i2sFlushZeros((TTS_SAMPLE_RATE/20)*4) post-flush for MAX98357A mute engage.
 *   - PSRAM fast-path, DC-blocking HP filter, PDM mic stop/restart, and
 *     SD fallback path all carried forward from playWAVSpeaker() equivalent.
 *   - playWAVSpeaker() itself is unchanged -- it remains correct at 16kHz
 *     for handleSpeakerPlayback() (recorded REC audio) and speakText()
 *     announcements (downsampled to 16kHz before saving).
 *
 * v1.2.36 CHANGE: ANALYSIS TTS RESPONSE -> BROWSER PLAYBACK (align with v3.2.4)
 * =================================================
 * The TTS response for analysis results now plays through the browser
 * <audio> element (auto-play on analysis complete) instead of the I2S
 * speaker. This matches the methodology used in the v3.2.4 standalone
 * camera firmware.
 *
 * Changes:
 *   - generateTTSAudio(): response_format "pcm" -> "wav", FIR downsampler
 *     and WAV header construction removed. OpenAI returns a 24kHz WAV
 *     directly -- saved as-is to SD. Browser plays it natively at 24kHz.
 *   - loop(): deferred pendingTTSPlay / playWAVSpeaker() block removed.
 *     pendingTTSPlay, pendingTTSPath, pendingTTSDelayMs vars removed.
 *   - JS: progress poll handler updated -- on resultReady, auto-plays
 *     ttsFile via the audioPlayer element (same pattern as v3.2.4).
 *     Status message updated: "Playing on speaker..." -> "Playing in browser..."
 *   - Audio playback bar: audioPlayer element already present in UI;
 *     now populated and auto-played on analysis completion.
 *
 * Retained on speaker (speakText() unchanged):
 *   - Startup announcement
 *   - All serial command confirmations (CMD:CAPTURE, CMD:RECORD, etc.)
 *   - Error announcements
 *   - PIN command audio feedback
 *   These are short phrases suited to the speaker; the full analysis
 *   response text is better delivered via browser audio.
 *
 * generateTTSAudio() now uses the same simple chunked WAV download as
 * v3.2.4 -- no FIR, no PSRAM downsampler, no WAV header construction.
 * The saved file is a standard 24kHz WAV served directly to the browser.
 *
 * v1.2.35 SIMPLIFY: REMOVE CAPTIVE PORTAL / CREDENTIALSMANAGER DEPENDENCY
 * =================================================
 * Removes the credentials_manager.h captive portal setup wizard and NVS
 * credential storage. Credentials are now stored directly in a lightweight
 * inline Preferences wrapper -- no AP mode, no DNS server, no setup page,
 * no BOOT button factory reset. This eliminates the credentials_manager.h
 * dependency entirely.
 *
 * Credential storage: Preferences (NVS) read/written inline via a simple
 * SettingsStore helper class defined in this file. On first boot (or if NVS
 * is empty), DEFAULT_* constants are used. The /settings web page remains
 * fully functional for live credential editing without reflashing.
 *
 * Removed from credentials_manager.h dependency:
 *   - startSetupWizard() captive portal AP + DNS server
 *   - BOOT button factory reset (3-second hold)
 *   - CredentialsManager static class with full NVS management
 *   - DNSServer and AP-mode WiFi setup
 *
 * Retained:
 *   - DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASSWORD / DEFAULT_OPENAI_API_KEY
 *     constants at top of file for first-flash configuration
 *   - /settings page for live credential editing (saves to NVS, reboots)
 *   - API key health banner, sanitization, maxlength on settings page
 *   - WiFiManager::connect() + monitor() from error_handling.h
 *   - ssl_validation.h (unchanged)
 *   - All audio DSP, automation agent, serial API, diagnostics unchanged
 *
 * v1.2.34 FIX: WiFiManager::monitor() GUARD + HEADER FILE UPDATES (v3.2.4)
 * =================================================
 * Root cause of noisy/halting audio identified in error_handling.h:
 *
 * WiFiManager::monitor() runs every 5 seconds from loop(). The TTS
 * chunked download loop calls vTaskDelay(1) between reads, which yields
 * back to loop() and allows monitor() to fire. If WiFi.status() returns
 * anything other than WL_CONNECTED during the download (transient glitch,
 * RSSI dip), monitor() calls attemptReconnection() which calls
 * WiFi.disconnect() -- killing the active HTTPS stream mid-download and
 * corrupting the PCM buffer. The resulting truncated/garbage audio
 * explained the noisy and halting playback observed since v1.2.32.
 *
 * Fix: error_handling.h v3.2.4 adds a guard at the top of monitor():
 *   extern volatile bool ttsInProgress;
 *   extern volatile bool analysisInProgress;
 *   if (ttsInProgress || analysisInProgress) return;
 * These extern declarations resolve to the volatile flags already defined
 * in this .ino file. No changes to the .ino are required.
 *
 * Also fixed in error_handling.h v3.2.4:
 *   - WiFi.setSleep(false) called after successful reconnection
 *     (connect() sets it but reconnect() did not -- modem sleep could
 *     re-enable, causing latency spikes on subsequent API calls)
 *   - generateTTSWithRetry(): voice alloy->nova, mp3->wav, chunked decoder
 *
 * Also fixed in credentials_manager.h v3.2.4:
 *   - Captive portal API key input: maxlength="220" added
 *   - API key sanitized before saveCredentials() in /configure handler
 *
 * No changes to the .ino itself -- all fixes are in the header files.
 * Flash this .ino alongside the updated error_handling.h and
 * credentials_manager.h from the v3.2.4 header package.
 *
 * v1.2.33 REVERT: RESTORE "pcm" FORMAT -- WAV HEADER SKIP CAUSED AUDIO CORRUPTION
 * =================================================
 * v1.2.32 changed response_format from "pcm" to "wav" and added a 44-byte
 * header skip before the FIR downsampler. In testing, audio was noisy and
 * halting on both speaker and web playback -- worse than v1.2.31.
 *
 * Root cause: the header skip block used signed/unsigned arithmetic that
 * produces undefined behaviour when a read returns fewer than 44 bytes:
 *   pcmLen (int) -= skip (size_t)  -> if result < 0, wraps as unsigned ->
 *   enormous nSrc passed to downsample24to16() -> garbage pointer arithmetic
 *   -> corrupted DST buffer written to PSRAM/SD -> audible noise and halting.
 *
 * The "pcm" format never had a header to skip, so this code path did not
 * exist in v1.2.31. "pcm" is also marginally more efficient -- no header
 * bytes to discard, and the chunked decoder can pass every byte directly
 * to the FIR without a skip state machine.
 *
 * Fix: revert both speakText() and generateTTSAudio() to response_format
 * "pcm" and remove the headerBytesConsumed / WAV_HEADER_SIZE logic entirely.
 * All DSP (FIR, shelf, DC-block, PSRAM, mic stop/start) is unchanged.
 * This restores v1.2.31 audio quality.
 *
 * v1.2.32 SIMPLIFY: REQUEST "wav" FORMAT FROM TTS API (align with v3.2.x camera)
 * =================================================
 * Previously: response_format="pcm" -> raw 24kHz PCM stream, no header.
 *   The downsampler loop manually constructed and wrote the WAV header to SD,
 *   then fed each raw PCM chunk through downsample24to16().
 *
 * Now: response_format="wav" -> 24kHz WAV with standard 44-byte header.
 *   - Chunked decoder is unchanged (same as v3.2.2 camera fix).
 *   - The first 44 bytes of the payload are the WAV header; they are
 *     buffered and skipped before any bytes reach downsample24to16().
 *   - The downsampler output is still written as a 16kHz WAV to SD
 *     (header built from scratch as before, since we convert the sample rate).
 *   - All DSP is preserved: 63-tap Kaiser FIR, HF shelf in FIR, DC-blocking
 *     HP filter in playback, TTS_PLAY_ATTENUATION, PSRAM fast-path, PDM mic
 *     stop/start. No audio quality regression.
 *
 * Simplification achieved:
 *   - Removed manual WAV header construction in speakText() (header was built
 *     by hand; now the "wav" response confirms sample rate / format we expect).
 *   - Removed manual WAV header construction in generateTTSAudio() same reason.
 *   - Header skip logic added: headerBytesConsumed tracks how many of the 44
 *     header bytes have been read; bytes are discarded until all 44 are gone,
 *     then normal PCM processing resumes. Handles the case where the header
 *     spans two HTTP chunks cleanly.
 *   - Both speakText() and generateTTSAudio() updated identically.
 *
 * v1.2.31 TEST: STOP PDM MIC DURING TTS PLAYBACK (crosstalk diagnostic)
 * =================================================
 * By v1.2.30 the audio DSP chain was verified clean:
 *   - True silence RMS = 7 (excellent)
 *   - Hard clipping: 0.028% (near zero)
 *   - Noisy 1s windows: 0/18
 *   - DC offset: removed by HP filter
 * Yet perceived noise through speaker was "indeterminate" improvement.
 * This indicates the noise source is outside the audio signal path.
 *
 * Most likely cause: PDM microphone I2S clock (GPIO38, ~1MHz PDM clock rate)
 * running continuously since setup(), coupling into I2S speaker signal lines
 * (GPIO42 DOUT, GPIO45 BCLK, GPIO46 LRCLK) on the compact DFR1154 PCB.
 *
 * Test fix: stop i2s_mic before playWAVSpeaker(), restart after.
 * i2s_mic.end() shuts down the PDM I2S peripheral and tristates GPIO38/39.
 * i2s_mic.begin() restarts it -- PDM needs ~10ms to settle, so a short delay
 * is added after restart before any recording can be triggered.
 *
 * If noise disappears -> PDM crosstalk confirmed, fix is permanent.
 * If noise unchanged -> hardware/speaker issue, no firmware fix possible.
 *
 * v1.2.30 FIX: MOVE HF SHELF INTO FIR DOWNSAMPLER -- ELIMINATE CLIPPING AT SOURCE
 * =================================================
 * v1.2.29 applied the HF shelf filter in playWAVSpeaker (I2S output stage).
 * Analysis of audio__17_.wav revealed this was insufficient -- the filter was
 * working spectrally (0/21 noisy windows in simulation) but 1713 samples
 * (0.494%, across 31.8% of 20ms windows) were hard-clipped in the SAVED WAV.
 * Clipping happens in downsample24to16() when the FIR output exceeds int16
 * range on loud consonants. No downstream filter can repair clipped samples
 * because the waveform information is destroyed before the WAV is written.
 * The playback shelf filtered the harmonics that survived, but the clipping
 * distortion itself remained audible -- hence "somewhat better" not "fixed".
 *
 * Fix: move the HF shelf into downsample24to16(), applied to each FIR output
 * sample BEFORE the constrain() clamp. This means:
 *   - The WAV saved to SD already has soft HF rolloff (no post-processing needed)
 *   - Peak levels are reduced before constrain() -> zero hard clipping
 *   - Simulation: 1713 clipped samples -> 0, noisy windows 13/21 -> 0/21
 *
 * Shelf parameters (alpha=0.5, wet=0.7, dry=0.3):
 *   500Hz: -0.3dB  |  2kHz: -2.5dB  |  4kHz: -4.5dB  |  8kHz: -5.5dB
 * Shelf state (shelf_lp_prev) preserved alongside FIR state across chunks.
 * Reset by firReset() at the start of each TTS stream.
 *
 * The HF shelf in playWAVSpeaker (v1.2.29) is removed -- it is no longer
 * needed and was causing double-filtering on the already-shelved WAV.
 * playWAVSpeaker retains only: DC-block HP -> attenuate -> stereo -> I2S.
 *
 * v1.2.29 FIX: HIGH-FREQUENCY SHELF FILTER IN TTS PLAYBACK
 * =================================================
 * Even with nova voice (v1.2.28), 8/21 one-second windows had alias/speech
 * ratio >0.15, audible as harshness through the MX1.25 speaker.
 * Simulation with alpha=0.3, wet=0.6 shelf filter on the actual WAV confirmed
 * 8/21 noisy windows -> 0/21.
 *
 * Filter design: 1-pole IIR lowpass blended with dry signal
 *   lp[n] = alpha * lp[n-1] + (1-alpha) * x[n]   (lowpass state)
 *   out[n] = dry * x[n] + wet * lp[n]             (shelf blend)
 *
 *   alpha = 0.3  ->  LP corner ~= 1783Hz
 *   wet   = 0.6  ->  HF shelf depth:
 *     500Hz: -0.1dB  |  2kHz: -0.8dB  |  4kHz: -2.0dB
 *     6kHz:  -2.6dB  |  8kHz: -2.8dB
 *
 * Implemented in fixed-point Q15 arithmetic (no floats):
 *   SHELF_ALPHA_Q15 = 9830   (0.3 x 32768)
 *   SHELF_1MA_Q15   = 22938  (0.7 x 32768)
 *   SHELF_WET_Q15   = 19661  (0.6 x 32768)
 *   SHELF_DRY_Q15   = 13107  (0.4 x 32768)
 *
 * Filter state (lp_prev) is reset to 0 at start of each file,
 * alongside the existing DC-blocking HP filter state.
 * Applied in both PSRAM and SD fallback paths in playWAVSpeaker().
 *
 * v1.2.28 CHANGE: TTS VOICE alloy -> nova
 * =================================================
 * Spectral analysis confirmed the remaining perceived noise is not an
 * artifact -- it is the natural high-frequency character of the "alloy"
 * voice. The 4-8kHz energy rises and falls with speech (sibilance pattern),
 * not independently (which would indicate aliasing).
 * The "alloy" voice is notably bright/forward and sounds harsh through the
 * small MX1.25 speaker on the DFR1154.
 * "nova" is a warmer voice with less high-frequency edge, better suited
 * to small speaker playback.
 * Changed in both speakText() and generateTTSAudio().
 *
 * v1.2.27 FIX: DC OFFSET IN TTS PCM -> IIR HIGH-PASS FILTER IN PLAYBACK
 * =================================================
 * Spectral analysis of audio__13_.wav revealed the root cause of all
 * remaining noise and the characteristic "thud" at sentence pauses:
 *
 * The OpenAI TTS 24kHz PCM stream contains a persistent DC offset of
 * ~1000 counts (+3% of full scale, -30dBFS). Sample values during
 * "silence" between sentences were ~1000, not ~0.
 *
 * This DC offset causes three distinct symptoms through the MAX98357A:
 *   1. THUD at pauses: DC steps from ~1000->0 or 0->1000 at sentence
 *      boundaries, creating an impulse that physically kicks the speaker cone.
 *   2. CONE DISPLACEMENT: sustained DC bias moves the speaker cone off its
 *      rest position, reducing excursion range and distorting all audio riding
 *      on top -- explains broadband noise throughout playback.
 *   3. TONAL ARTIFACT: autocorrelation found a periodic component at 1143Hz
 *      (lag=14 samples at 16kHz) -- the DC interacting with the 3:2 decimation
 *      phase pattern.
 *   4. CLEAR SPOTS: the 3-4 clear-sounding windows are the brief moments
 *      where the DC happened to be near zero.
 *
 * The FIR downsampler correctly passes DC at unity gain (by design).
 * The DC is in the TTS source -- not introduced by any firmware processing.
 *
 * Fix: 1st-order IIR DC-blocking high-pass filter in playWAVSpeaker(),
 * applied to each sample before the mono->stereo expand and I2S write:
 *
 *   y[n] = x[n] - x[n-1] + HP_ALPHA * y[n-1]
 *   HP_ALPHA = 0.9999  ->  cutoff ~= 1.6Hz at 16kHz
 *
 * This removes DC completely (-200dB at 0Hz) while passing all speech
 * frequencies unchanged (-0.0dB at 10Hz and above). The saved WAV on SD
 * is not modified -- filtering happens only at the I2S output stage.
 * Filter state (x_prev, y_prev) is reset to zero before each playback.
 *
 * v1.2.26 FIX: TTS PLAYBACK OVERDRIVING MAX98357A -> ADD PLAYBACK ATTENUATION
 * =================================================
 * All previous versions played TTS WAV at full digital scale (0 dBFS peak).
 * The MAX98357A amplifies the I2S signal at 9dB (GAIN pin floating = default).
 * A 0 dBFS I2S input at 9dB analog gain clips the amplifier output stage,
 * producing the persistent noise heard through the speaker.
 *
 * Evidence: audio__13_.wav peak = 32768 (0.0 dBFS), RMS = 9216 (-11.0 dBFS),
 * 0.6% of samples hard-clipped even in the saved WAV before playback.
 *
 * Fix: add TTS_PLAY_ATTENUATION constant (default 2 = right-shift by 1 = -6dB).
 * Applied in playWAVSpeaker() just before the mono->stereo expand, so:
 *   - The saved WAV on SD is unchanged (still represents true signal level)
 *   - Only the I2S output is attenuated
 *   - Peak I2S value = 32767 >> 1 = 16383 (50% of full scale)
 *   - MAX98357A at 9dB gain: 16383 -> clean analog output, no clipping
 *   - Effective SPL reduction ~6dB at speaker; still clearly audible
 *
 * TTS_PLAY_ATTENUATION values:
 *   0 = no attenuation (full scale, clips MAX98357A) -- DO NOT USE
 *   1 = -3dB  (peak 23,170 -- borderline safe)
 *   2 = -6dB  (peak 16,383 -- safe default, recommended)
 *   3 = -9dB  (peak 11,585 -- quieter but very clean)
 * Adjust in the user-configurable constants block at top of file.
 *
 * v1.2.25 FIX: FIR ACCUMULATOR int32 OVERFLOW -> int64
 * =================================================
 * v1.2.24 still produced noise. WAV analysis revealed hard clipping at 0 dBFS
 * in 15 of 18 windows (0.64% of samples = 1,872 hard-clipped frames).
 * Clipping generates harmonics across the full spectrum including 6-8kHz,
 * explaining the persistent ~20% alias ratio.
 *
 * Root cause: FIR accumulator declared as int32_t.
 *   Max possible value: 63 taps x 32767 x 19659 (max coeff) = 40,582,486,539
 *   int32 max:                                                  2,147,483,647
 *   Overflow factor: 18.9x -- the accumulator wraps on every loud consonant.
 *   The constrain() call was correct but operating on an already-wrong value.
 *
 * Fix: change `int32_t acc` -> `int64_t acc` in downsample24to16().
 *   ESP32-S3 (Xtensa LX7) handles 64-bit arithmetic natively.
 *   >> 15 shift works identically on int64.
 *   After shift, max output = 67,187 -> constrain to +/-32767 clips correctly.
 *   4 billionx headroom in int64 -- no further overflow possible.
 *
 * v1.2.24 FIX: FIR COEFFICIENT NORMALIZATION + UPGRADE TO 63-TAP KAISER FILTER
 * =================================================
 * v1.2.23's 31-tap Hann-windowed FIR had two problems confirmed by WAV analysis:
 *   1. Coefficients summed to 9827 instead of 32768 -- only 30% unity gain.
 *      This caused -10.5 dB volume loss (audio ~1/3 of expected level).
 *      The MAX98357A was near its auto-mute threshold, causing noise bursts.
 *   2. Hann window provides only ~28dB stopband rejection -- marginal.
 *
 * Fix: Replace with 63-tap Kaiser window (beta=6) FIR:
 *   - Coefficients properly normalized: sum=32769 ~= unity gain (0.00 dB)
 *   - Stopband rejection: 69 dB (vs 28dB Hann, vs 13dB linear interpolation)
 *   - Cutoff: 7200 Hz (below 8000 Hz Nyquist of 16kHz output)
 *   - Stack buffer: 447 int16 = 894 bytes (safe for ESP32-S3 task stack)
 *   - FIR_TAPS constant updated 31->63; firState array updated 30->62 samples
 *
 * v1.2.23 FIX: CORRECT API KEY LENGTH THRESHOLDS (164 chars IS valid)
 * =================================================
 * All firmware versions through v1.2.22 assumed OpenAI API keys were ~56 chars
 * (the old sk-proj-* format from before August 2024). This caused false
 * "SUSPICIOUS / corrupted" warnings for modern keys, which are ~164 chars.
 * The 56-char assumption was incorrect -- OpenAI increased key length in late 2024.
 * Modern sk-proj-* keys are ~156-168 chars; both old and new formats are valid.
 * Fix: keyHealthFromLen() and settings page now flag only <20 or >220 chars.
 * The 164-char key in NVS was never corrupted -- it was always valid.
 *
 * v1.2.23 FIX: REVERT pcm_16000 + PROPER ANTI-ALIAS FIR FILTER FOR DOWNSAMPLING
 * =================================================
 * v1.2.22 introduced response_format="pcm_16000" which does not exist in the
 * OpenAI TTS API. Valid formats are mp3, opus, aac, flac, wav, pcm only.
 * "pcm" always returns 24kHz. The API rejected the unknown format, returning
 * a non-200 response, producing no WAV file and a 4,294,896,391ms TTFB sentinel.
 *
 * Fix: Revert to response_format="pcm" (24kHz) and replace the linear
 * interpolation downsampler with a proper windowed-sinc FIR low-pass filter
 * before 3:2 decimation. This eliminates aliasing at the source.
 *
 * The FIR filter design:
 *   - 31-tap symmetric FIR (30th order), computed at compile time as constexpr
 *   - Cutoff: 7,500 Hz (just below the 8,000 Hz Nyquist of the 16kHz output)
 *   - Window: Hann window for ~44dB stopband rejection vs ~13dB for linear interp
 *   - Coefficients normalised so sum = 1.0 (unity DC gain)
 *   - Filter state (30 samples) preserved across HTTP chunks via static array
 *     so the filter operates continuously across chunk boundaries
 *   - After filtering, simple 3:2 decimation (take every 3rd sample of every
 *     2 output samples) replaces the fractional-position posCarry interpolator
 *
 * Also: API key maxlength restored to 200 (was accidentally set to 100 in
 * v1.2.20, truncating keys on entry and causing persistent 164/100-char issues).
 *
 * v1.2.22 FIX: ELIMINATE ALIASING NOISE -- REQUEST 16kHz DIRECTLY FROM TTS API
 * =================================================
 * Spectral analysis of the saved WAV file confirmed the noise is aliasing,
 * not DMA starvation or buffer corruption:
 *   - Clean speech window:  speech band 52,227  vs alias band  2,285  (4%)
 *   - Noise burst window:   speech band 85,410  vs alias band 103,321 (55%)
 * Alias energy exceeds speech energy in noise bursts -- high-frequency content
 * from the 24kHz TTS stream (8-12kHz) is folding back into the 0-4kHz audible
 * band because linear interpolation has no meaningful anti-alias low-pass filter.
 * Stopband rejection of linear interp at 3:2 ratio is only ~13dB -- far below
 * the ~60dB needed for transparent downsampling.
 *
 * Fix: Request pcm_16000 format from the OpenAI TTS API instead of pcm.
 * pcm returns 24kHz raw PCM; pcm_16000 returns 16kHz raw PCM directly.
 * This eliminates the downsampleLinear() call and posCarry mechanism entirely
 * for both speakText() and generateTTSAudio(). No downsampling = no aliasing.
 *
 * Also fix: maxlength on API key input raised from 100 to 200. The v1.2.20
 * cap of 100 chars truncated keys longer than 100 chars on entry. A real
 * sk-proj-* keys were ~56 chars before Aug 2024; modern keys are ~164 chars.
 *
 * downsampleLinear() function is retained in the codebase but no longer called.
 *
 * v1.2.21 FIX: AUDIO PLAYBACK NOISE -- vTaskDelay IN PLAYBACK LOOP (LARGE FILES)
 * =================================================
 * Live run diagnostics showed repeating noise->clear->noise pattern throughout
 * 19s of audio playback (609KB PCM, 149 SD read chunks, 18 yield points).
 *
 * Root cause: vTaskDelay(1) at line 1118 nominally yields for 1ms but on a
 * loaded ESP32-S3 with WiFi active, the RTOS scheduler can steal 10-50ms per
 * yield. 18 yields x 50ms = up to 900ms of DMA starvation spread through the
 * file. DMA drains -> MAX98357A sees no I2S clock -> noise burst until the next
 * i2s_spk.write() refills the buffer. Same root cause as v1.2.10, but masked
 * during development by shorter TTS responses; exposed by longer live responses.
 *
 * Fix: Two-stage approach in playWAVSpeaker():
 *   1. PSRAM fast-path: if the WAV fits in available PSRAM (checked via
 *      heap_caps_get_free_size), load the entire file into PSRAM before
 *      starting I2S. Feed I2S from ram pointer -- no SD latency during playback.
 *      SD reads complete in <1ms from PSRAM vs 5-50ms from SD_MMC.
 *   2. Replace vTaskDelay(1) with taskYIELD() in both PSRAM and SD fallback
 *      paths. taskYIELD() returns immediately if no higher-priority task is
 *      ready, vs vTaskDelay(1) which always sleeps for >=1 RTOS tick (1ms)
 *      and can be preempted for much longer under scheduler load.
 *   3. Yield interval reduced from every 8 chunks to every 32 chunks in the
 *      SD fallback path, reducing yield frequency by 4x for large files.
 *
 * v1.2.20 FIX: API KEY INPUT HARD LIMIT + DIAG PANEL LABEL BUG
 * =================================================
 * After v1.2.19 was flashed, diagnostics still showed 164-char key with [OK]
 * instead of the expected [!] SUSPICIOUS LENGTH warning. Two bugs found:
 *
 * Bug 1 -- JS label ternary short-circuit: the label expression
 *   `apiKeyLen>80 ? '[!] SUSPICIOUS' : '[OK]'`
 *   was being overridden because the color ternary rendered green for >=40,
 *   creating a visual mismatch where color and label disagreed. Fixed by
 *   extracting key health into a separate JS helper function `keyStatus(n)`
 *   that returns both color and label atomically, eliminating the dual-ternary.
 *
 * Bug 2 -- Settings page input has no maxlength: a 164-char key could be
 *   pasted into the API key field even after the red warning appeared.
 *   Fixed: `maxlength="100"` added to the apikey input. A valid OpenAI key
 *   is ~56 chars; 100 gives headroom while making doubled keys impossible.
 *   Also added a live char-counter below the field (e.g. "42 / 100 chars").
 *
 * v1.2.19 FIX: SANITIZE API KEY ON WRITE + SETTINGS PAGE KEY HEALTH DISPLAY
 * =================================================
 * Diagnostics revealed the stored API key was 164 chars -- roughly 3x a valid
 * key length (~56 chars for sk-proj-* keys). sanitizeAPIKey() was stripping
 * whitespace on READ but the corrupt value was already persisted in NVS.
 *
 * Fix: sanitize on WRITE in handleSettingsSave() before calling
 * SettingsStore::saveCredentials(). Garbage can no longer enter NVS.
 *
 * Additional changes:
 *   - Settings page shows stored key health: length + first 4 / last 4 chars
 *     so the user can verify NVS content without exposing the full key
 *   - Red warning banner on settings page if key length is suspicious:
 *     < 20 chars (placeholder/truncated) or > 100 chars (corrupted/doubled)
 *   - handleSettingsSave() logs sanitized key length to Serial before save
 *   - apiKeyLen color thresholds in diag panel tightened:
 *     green 40-80 chars (valid range), yellow 10-39, red otherwise
 *     (previously green > 10, which showed green for the corrupt 164-char key)
 *
 * v1.2.18 FEATURE: PIPELINE TIMING DIAGNOSTICS
 * =================================================
 * Adds a non-destructive timing instrumentation layer that measures every
 * significant processing stage and reports results to the Serial Monitor AND
 * to the web UI via a dedicated /diagnostics endpoint.
 *
 * TIMING STAGES MEASURED (all times in milliseconds):
 *   T0  - Analysis task accepted (POST /openai/analyze received)
 *   T1  - Image JPEG opened from SD
 *   T2  - Base64 encoding complete                 -> T2-T1 = encoding time
 *   T3  - GPT-4 Vision HTTP POST sent
 *   T4  - GPT-4 Vision HTTP response received      -> T4-T3 = network RTT
 *   T5  - GPT-4 JSON parsed, response string ready -> T5-T0 = vision total
 *   T6  - TTS HTTP POST sent
 *   T7  - First TTS byte received from OpenAI      -> T7-T6 = TTS server TTFB
 *   T8  - Last TTS byte received / download done   -> T8-T7 = TTS stream time
 *   T9  - PSRAM->SD write complete                  -> T9-T8 = SD write time
 *   T10 - pendingTTSPlay flag set                  -> T10-T0 = total pre-play
 *   T11 - playWAVSpeaker() called                  -> T11-T10 = UI delay hold
 *   T12 - playWAVSpeaker() returns                 -> T12-T11 = playback duration
 *
 * ADDITIONAL METRICS PER RUN:
 *   - Image file size (bytes)
 *   - Base64 output length (chars)
 *   - Audio WAV file size (bytes)
 *   - PCM bytes buffered in PSRAM
 *   - WAV file size written to SD
 *   - PSRAM free before/after allocation
 *   - Heap free at analysis start
 *   - WiFi RSSI at analysis start
 *   - Any error stages (which stage failed and at what time)
 *
 * IMPLEMENTATION NOTES:
 *   - All timing uses millis() -- no hardware timer needed, 1ms resolution
 *   - PipelineTiming struct holds all fields; one instance per run (stack)
 *   - struct passed by reference through the call chain so no globals needed
 *   - Zero overhead when diagnostics not running (struct only lives during task)
 *   - /diagnostics GET endpoint: returns JSON of last completed run
 *   - /diag/clear DELETE endpoint: wipes stored results
 *   - Web UI: "Diagnostics" button added to header; opens overlay panel showing
 *     a formatted timeline with each stage labeled and delta highlighted in
 *     color (green < 1s, yellow 1-5s, red > 5s) so bottlenecks are obvious
 *   - Serial output: full timing table printed at end of every analysis run
 *     regardless of success/failure so even partial runs are diagnosable
 *
 * SYNTHETIC TEST DATA (/test/pipeline endpoint):
 *   The /test/pipeline POST endpoint runs the entire pipeline with SYNTHETIC
 *   inputs -- no real camera capture or microphone recording needed. It:
 *     1. Generates a small synthetic JPEG (solid-color, ~2KB) in PSRAM
 *     2. Captures a real camera frame via esp_camera_fb_get() and saves it
 *        as /images/TEST_IMG.jpg -- the original embedded JPEG stub was
 *        rejected by GPT-4 Vision (malformed Huffman table). Using the live
 *        camera also tests the actual capture path and produces a realistic
 *        image file size in the diag metrics.
 *     3. Uses a hardcoded audio transcription bypass string instead of Whisper
 *     4. Runs real GPT-4 Vision + real TTS calls with real network
 *     5. Runs real PSRAM buffer + SD write + playWAVSpeaker()
 *   This lets you verify every pipeline stage and collect timing data without
 *   needing a physical audio prompt -- useful for CI-style regression testing
 *   after each firmware change.
 *
 * FIXES vs initial v1.2.18 release:
 *   - Synthetic test: replaced embedded JPEG stub with live camera capture
 *     (esp_camera_fb_get). The stub was malformed and rejected by GPT-4 Vision.
 *   - PSRAM metric: replaced ESP.getFreePsram() (returns 0 until first use on
 *     some builds) with heap_caps_get_free_size(MALLOC_CAP_SPIRAM) which reads
 *     the PSRAM heap directly and is reliable from first call.
 *   - Vision error path: now logs the HTTP response body to Serial so rejection
 *     reasons are visible without needing a network proxy.
 *
 * v1.2.17 FEATURE: PERSISTENT TTS FILE INDEX + RESOURCES PANEL
 * =================================================
 * Previously, TTS response WAV files were saved to /tts/ but were never
 * indexed or surfaced in the web UI. Each new analysis run named its TTS
 * file "TTS_<analysisID>.wav" (unique per run), so old files accumulated
 * invisibly on the SD card and could only be accessed via the export
 * package download immediately after analysis -- if a new analysis ran
 * first, the previous TTS was unreachable from the UI.
 *
 * Changes:
 *   - ttsFileList vector added (mirrors imageFileList / audioFileList)
 *   - refreshTTSList() scans /tts/ and rebuilds the vector; called after
 *     every generateTTSAudio() and speakText() call that saves a file
 *   - /list/tts endpoint returns JSON file list for the web UI
 *   - /delete/tts endpoint removes a named file from /tts/ and refreshes
 *   - Web UI: new "TTS Files (Resources)" panel added to the files-section
 *     alongside the existing Image/Audio panels. Each entry shows:
 *       [Speaker] -- plays the file through the I2S speaker via /tts/speaker
 *       [Web]     -- loads the file into the browser audio player via /tts
 *       [Delete]  -- removes the file from SD
 *   - Panel auto-refreshes after every analysis (same as image/audio lists)
 *   - The TTS panel is a full third column in the files grid, making all
 *     three resource types (image, audio, TTS) equally accessible and
 *     persistent across sessions and analysis runs
 *
 * v1.2.16 PATCH: GUARANTEE BROWSER RECEIVES RESULT BEFORE AUDIO PLAYS
 * =================================================
 * Symptom: The OpenAI text result appeared in the web UI AFTER the speaker
 * finished playing, not before. The deferred-playback mechanism introduced in
 * v1.2.9 was supposed to fix this, but v1.2.15's PSRAM buffering made the
 * problem worse: generateTTSAudio() now holds the CPU for the full download
 * duration before returning, so by the time analysisResultReady is set and
 * pendingTTSPlay is set, loop() picks up pendingTTSPlay on the very NEXT
 * iteration -- leaving only a single server.handleClient() call between
 * "result ready" and "speaker blocking". The browser polls every 1500ms;
 * one handleClient() cycle is not enough to guarantee the browser sees
 * resultReady:true before playback begins.
 *
 * Fix: Add a pendingTTSDelayMs timestamp alongside pendingTTSPlay. loop()
 * will NOT start playback until at least 3000ms after the flag is set,
 * servicing server.handleClient() continuously during that window. This
 * guarantees the browser has time to complete at least two full poll cycles
 * (each 1500ms) and receive + render the result text before the I2S playback
 * begins blocking the web server.
 *
 * v1.2.15 PATCH: BUFFER FULL TTS RESPONSE IN PSRAM -- ELIMINATE NETWORK STALL GAPS
 * =================================================
 * WAV analysis confirmed 4 true silence gaps (271ms, 731ms, 313ms, 406ms) caused
 * by OpenAI TTS server pauses between synthesis segments. During these pauses the
 * chunked decoder received no data and wrote zeros to the WAV file, inserting
 * silence mid-sentence. The lastDataMs fix in v1.2.14 reduced but did not
 * eliminate these gaps -- the server simply stalls longer than any practical timeout.
 *
 * Fix: Buffer the entire downsampled PCM stream into PSRAM before writing the WAV.
 * The ESP32-S3 has 8MB OPI PSRAM; a 45s response at 16kHz mono 16-bit needs ~1.4MB
 * (5.8x headroom). Network stalls cause no silence in the output because nothing is
 * written to the WAV during them -- the buffer just waits. After the stream ends the
 * complete PCM is written to SD in one pass with the correct WAV header prepended.
 *
 * Both speakText() and generateTTSAudio() updated. The PSRAM buffer is allocated
 * with ps_malloc() and freed after the SD write. If ps_malloc fails (extremely
 * unlikely given headroom), both functions fall back gracefully to the old
 * direct-to-SD behavior so the device never crashes silently.
 *
 * v1.2.14 PATCH: FIX MICRO-DROPOUTS AND CHUNK TAIL BYTE LOSS IN TTS STREAM
 * =================================================
 * WAV silence gap analysis revealed two problems in the chunked PCM decoder:
 *
 * 1. TAIL BYTE LOSS (causes ~1ms dropout per HTTP chunk, hundreds per file):
 *    The inner read loop applied `toRead -= (toRead % 6)` to align reads to
 *    6-byte boundaries (3 x int16 at 24kHz). When `remaining` dropped below
 *    6, `toRead` was rounded to 0 and the loop stalled, never consuming the
 *    last 1-5 bytes of the chunk payload. Those samples were dropped silently,
 *    creating a micro-gap at every chunk boundary throughout the audio.
 * - Fix: Remove the 6-byte alignment constraint from the inner read loop.
 *    Alignment to even bytes (2) is sufficient since the downsampler just
 *    needs complete int16 pairs. The `posCarry` mechanism already handles
 *    fractional sample positions across calls, so partial chunks are fine.
 *    `toRead -= (toRead % 2)` applied only when toRead >= 2; if toRead == 1
 *    the single byte is read and the length is forced to 0 (len%2 clips it).
 *
 * 2. LARGE SILENCE GAPS (100-935ms, network stall artifacts):
 *    During slow network delivery, the outer while loop timed out between
 *    chunk-size reads, leaving a silence gap in the WAV where no data arrived.
 *    lastDataMs only reset on successful payload reads, not on chunk-size
 *    line reads, so a slow-arriving chunk-size line could timeout the loop.
 * - Fix: Reset lastDataMs after each successfully read chunk-size line as
 *    well as after payload data, so the timeout only fires on true stalls.
 *    Also extend the inter-chunk timeout to 15s (generateTTS) since long
 *    responses can have slow inter-chunk gaps on congested networks.
 *
 * v1.2.13 PATCH: FIX HTTP CHUNKED ENCODING HEADERS WRITTEN INTO WAV FILE
 * =================================================
 * WAV file hex dump confirmed: the very first bytes of PCM data were
 * "31 35 0d 0a" = ASCII "15\r\n" -- an HTTP chunked transfer chunk-size
 * header line. The last two bytes were "0d 0a" = trailing CRLF after the
 * final chunk. These framing bytes were passed into the downsampler and
 * written to the WAV as PCM audio, producing the clicks/spikes at the
 * beginning and end of every TTS playback.
 *
 * Root cause: https.getStreamPtr() returns the raw TCP transport stream.
 * When OpenAI TTS uses chunked transfer encoding (no Content-Length),
 * readBytes() on the raw stream reads chunk framing bytes interleaved
 * with PCM payload -- no automatic decoding occurs at this layer.
 *
 * Fix: Both speakText() and generateTTSAudio() now use a chunked-decode
 * read loop. Each iteration reads the hex chunk-size line, reads exactly
 * that many payload bytes, then consumes the trailing \r\n. rawBuf
 * contains only pure PCM before the downsampler sees it. Chunk size 0
 * signals end of body.
 *
 * v1.2.12 PATCH: FIX BEGINNING AND END NOISE -- MAX98357A MUTE TRANSIENTS
 * =================================================
 * WAV file analysis confirmed audio content is now clean throughout. The
 * remaining "beginning and ending noise" is not in the recorded data --
 * it is caused by the MAX98357A amplifier's built-in pop-suppression
 * (auto-mute) circuit interacting with the pre/post flush silence.
 *
 * BEGINNING NOISE -- pre-flush triggering mute/unmute pop:
 *   The 80ms silence pre-flush was intentionally feeding silence before
 *   audio to prime the DMA. However the MAX98357A monitors its input and
 *   automatically mutes when it detects silence. The 80ms silence flush
 *   engaged the mute, then the first loud audio sample caused an abrupt
 *   unmute -- a pop/click at the very start of speech.
 * - Fix: Remove the pre-flush entirely from playWAVSpeaker(). The DMA
 *   pipeline is already clean from the previous post-flush, and the
 *   amplifier stays in its active (unmuted) state if signal follows
 *   immediately. First audio chunk goes directly into the DMA.
 *
 * ENDING NOISE -- abrupt signal cutoff triggering output transient:
 *   After the last audio frame, post-flush silence was written immediately.
 *   The abrupt high-amplitude -> zero transition caused a DMA output spike
 *   before the amplifier's mute circuit could engage -- an audible click.
 * - Fix: Add a 32ms linear fade-out applied to the last audio buffer
 *   before the post-flush silence. This ramps the signal smoothly to zero,
 *   allowing the MAX98357A mute circuit to engage on a natural decay
 *   rather than a hard cut. Post-flush silence then holds the mute state.
 *
 * v1.2.11 PATCH: FIX DOWNSAMPLER PHASE DISCONTINUITY ACROSS HTTP STREAM CHUNKS
 * =================================================
 * Root cause confirmed by WAV file analysis:
 *   The OpenAI TTS stream is received in chunks (~768 raw bytes = 384 samples
 *   at 24kHz). The 24kHz->16kHz downsample ratio is 3:2 -- NOT an integer
 *   boundary. Every 768-byte chunk leaves a fractional sample position that
 *   does not land exactly on a 16kHz output boundary. The old downsampleLinear()
 *   signature discarded this remainder: pos always reset to 0 at the start of
 *   each chunk call. This created a phase discontinuity at every chunk boundary
 *   -- a small but repeated waveform glitch that accumulated into audible
 *   distortion and a lowered perceived SNR (sounded "noisy").
 *
 *   The WAV analysis also showed that the first ~6s of the TTS file was 6-8 dB
 *   quieter than the remainder (-10 dB vs -6 dB RMS) -- consistent with the
 *   early chunks having higher distortion and thus lower apparent loudness
 *   after the phase errors were written to disk.
 *
 * Fix: downsampleLinear() gains a uint32_t& posCarry parameter (passed by
 *   reference). The caller initializes posCarry=0 before the stream loop and
 *   passes it on every chunk call. At the end of each chunk the function
 *   subtracts the consumed source samples from pos and writes the remainder
 *   back to posCarry. The next chunk call starts at the correct fractional
 *   position, producing a phase-continuous output stream across all chunk
 *   boundaries. Both speakText() and generateTTSAudio() updated accordingly.
 *
 * v1.2.10 PATCH: ELIMINATE REMAINING 50% NOISE -- PER-CHUNK vTaskDelay IN PLAYBACK LOOP
 * =================================================
 * Root cause of remaining ~50% noise during TTS playback:
 *   playWAVSpeaker() called vTaskDelay(1) after EVERY 1024-byte SD read chunk.
 *   At 16kHz stereo 16-bit the I2S DMA clocks out 64 bytes/ms. A single
 *   vTaskDelay(1) yields for at least 1 RTOS tick; under load the scheduler
 *   may not return for several ms. During that window the DMA exhausts its
 *   buffer and outputs stale/garbage memory -- a noise burst. The next chunk
 *   then arrives and plays clean. This produces the alternating clean/noise
 *   pattern: one chunk clean, yield, one chunk noise, repeat -- consistent
 *   with the reported ~50% clarity.
 *
 * Fix 1: Remove vTaskDelay(1) from inside the per-chunk playback loop.
 *   The DMA must be kept continuously fed. WDT is not an issue here because
 *   i2s_spk.write() itself yields internally to the RTOS while waiting for
 *   DMA buffer space -- the scheduler still runs, WDT still resets.
 *
 * Fix 2: Increase mono read buffer from 1024 -> 4096 bytes (stereo 8192).
 *   Larger reads reduce the number of SD access round-trips per second,
 *   lowering the probability of SD latency causing a DMA gap. Each chunk
 *   now provides ~128ms of audio headroom (vs ~32ms before) -- SD reads
 *   have to take longer than 128ms to cause an underrun, which is unlikely.
 *
 * Fix 3: Add coarse yield every 8 chunks (every ~1 second of audio) via
 *   a simple counter. This satisfies any WDT/RTOS requirements without
 *   interrupting the DMA stream at the high-frequency rate that caused noise.
 *
 * v1.2.9 PATCH: DECOUPLE TTS PLAYBACK FROM ANALYSIS + FIX DMA UNDERRUN
 * =================================================
 * ISSUE 1 -- Result text blocked until audio ends:
 *   playWAVSpeaker() was called directly inside runOpenAIAnalysis() after
 *   setting analysisResultReady=true. The ESP32 WebServer is single-threaded;
 *   while playWAVSpeaker() blocks in its SD read/I2S write loop, no HTTP
 *   requests can be served. The browser's /openai/progress poll therefore
 *   cannot get a response until playback finishes, so the result text never
 *   appeared until after the audio ended.
 * - Fix: Removed playWAVSpeaker() call from runOpenAIAnalysis(). Added a
 *   pending playback flag (pendingTTSPlay / pendingTTSPath). loop() checks
 *   this flag and calls playWAVSpeaker() from there, *after* the next
 *   server.handleClient() cycle -- giving the browser at least one full HTTP
 *   response window to receive and display the result text before audio starts.
 *
 * ISSUE 2 -- Noise worse than before (DMA underrun during settle delay):
 *   The delay(20) added in v1.2.8 after the pre-flush is a hard RTOS block.
 *   During those 20ms the I2S DMA continues clocking out data; once the
 *   silence buffer is exhausted the DMA wraps and replays stale/garbage data.
 *   This is the "half noise" pattern -- silence buffer empties, garbage plays,
 *   then real audio arrives.
 * - Fix: Removed the hard delay(20) settle. Instead the pre-flush is extended
 *   to 80ms worth of silence (SAMPLE_RATE/12*4 bytes) which continuously
 *   feeds the DMA throughout the settle window -- no underrun possible. The
 *   MAX98357A sees clean silence signal for the full settle period.
 * - i2sFlushZeros() also now calls i2s_spk.write() in a tight loop without
 *   vTaskDelay between chunks -- yielding every 512 bytes caused micro-gaps
 *   in the silence stream that could trigger brief noise bursts. Yield is
 *   moved to once per full flush call instead.
 *
 * v1.2.8 PATCH: FIX TTS LEADING NOISE / DMA BUFFER UNDERRUN
 * =================================================
 * - Root cause of leading noise / intelligibility shift during TTS playback:
 *   i2sFlushZeros() was counting bytes but never writing silence to I2S DMA.
 *   This left stale DMA buffers filled with garbage at playback start, producing
 *   the characteristic "noise then audio" pattern as DMA gradually filled with
 *   real samples and pushed the garbage out.
 * - Fix: i2sFlushZeros() now actually writes zero-filled stereo frames to
 *   i2s_spk via i2s_spk.write(), using a static 512-byte silence buffer to
 *   stay off the stack. Writes in 512-byte chunks until requested byte count
 *   is satisfied.
 * - playWAVSpeaker() pre-flush corrected: was passing (SAMPLE_RATE / 100) as
 *   a sample count but i2sFlushZeros() expects bytes. Now passes
 *   (SAMPLE_RATE / 100 * 4) -- correct byte count for 16kHz stereo 16-bit
 *   (~10ms of silence = 640 bytes).
 * - Added 20ms post-prime settle delay in playWAVSpeaker() after the pre-flush
 *   to allow MAX98357A amplifier to stabilize on the silence signal before
 *   first audio frame arrives.
 * - Post-flush at end of playback updated to matching byte count for symmetry.
 *
 * v1.2.7 PATCH: FIX STACK OVERFLOW + CORRECT WDT APPROACH
 * =================================================
 * - Stack canary crash (EXCCAUSE 0x01) caused by large local buffers in audio
 *   functions consuming loopTask stack (default ~8KB):
 *   speakText: rawBuf[768]+dstBuf[1024], generateTTSAudio: same,
 *   playWAVSpeaker: mono[1024]+stereo[2048] = ~5KB+ when nested
 * - Fix: all large audio buffers made STATIC (moved to BSS, off stack)
 * - v1.2.6 used esp_task_wdt_reset() which requires task subscription first;
 *   this caused "task not found" error spam on every call
 * - Fix: removed esp_task_wdt.h entirely; replaced with vTaskDelay(1) which
 *   yields to RTOS scheduler and implicitly resets WDT for subscribed tasks
 * - i2sFlushZeros: removed fed counter and wdt calls; static zeros buffer
 *
 * v1.2.5 PATCH: ELIMINATE NOISE -- DOWNSAMPLE TTS TO 16kHz, FIX TEXT DELAY
 * =================================================
 * - Root cause of persistent noise identified: repeated I2S end()/begin() calls
 *   cause the MAX98357A to lose clock lock; any data written during relock is
 *   output as noise. Fix: never reinitialize I2S. Instead downsample TTS PCM
 *   from 24kHz to 16kHz in software (3:2 linear interpolation) before saving
 *   to SD, so I2S stays at 16kHz stereo throughout -- identical to the original
 *   working v1.1.0 configuration.
 * - TTS WAV files are now saved at 16kHz (matching recorded audio) so web
 *   browser playback also receives the correct sample rate
 * - playWAVSpeaker() simplified: no reinit, no slot mode switching, always
 *   16kHz stereo, mono->stereo duplication (same as original working code)
 * - Analysis result text and replay buttons now appear on web UI BEFORE
 *   speaker playback begins (result was previously blocked until audio ended)
 * - speakText() announcements also downsampled to 16kHz before saving
 *
 * v1.2.4 PATCH: FIX I2S MONO SLOT MODE FOR TTS PLAYBACK
 * =================================================
 * - Root cause of 2/3 noise: I2S_SLOT_MODE_STEREO with mono-duplicated data
 *   causes the ESP32 I2S peripheral to misalign 16-bit samples within 32-bit
 *   DMA frames, producing noise on every other sample boundary
 * - playWAVSpeaker() now reads slot count from WAV header (numChannels field)
 *   and reinitializes I2S in MONO mode for mono files (TTS) and STEREO for
 *   stereo files -- writes raw samples with no duplication in mono mode
 * - i2sFlushZeros() updated to match current slot mode
 * - handleSpeakerPlayback() (recorded WAV) updated to same pattern for consistency
 *
 * v1.2.3 PATCH: ELIMINATE I2S NOISE / SILENT SECTION STATIC
 * =================================================
 * - playWAVSpeaker() now flushes I2S DMA with explicit zero frames before
 *   and after playback -- eliminates stale-DMA noise during silent sections
 *   and trailing noise after audio ends
 * - Added 50ms settle delay after I2S reinit before feeding first audio data
 *   (MAX98357A amplifier needs time to stabilize after sample rate change)
 * - Increased SD read buffer from 256 to 1024 bytes (stereo 2048) to reduce
 *   SD read gaps that caused momentary DMA underruns / noise bursts
 * - i2sFlushZeros() helper writes N frames of silence to drain/reset DMA
 *
 * v1.2.2 UPDATE: RELIABLE TTS + DUAL REPLAY
 * =================================================
 * - TTS WAV now written directly to SD card chunk-by-chunk during download
 *   (eliminates PSRAM buffer overflow and audio corruption on longer responses)
 * - WAV header written as a placeholder first; data size patched after download
 * - Analysis TTS response auto-plays through I2S speaker on completion
 * - Web audio player no longer auto-plays TTS (avoids browser/speaker conflict)
 * - New /tts/speaker endpoint plays any TTS file through the I2S speaker
 * - Replay TTS button in web UI now offers two options:
 *     [Speaker] plays through the physical speaker
 *     [Web]     plays through the browser audio player
 *
 * v1.2.1 PATCH: FIX TTS WAV PLAYBACK
 * =================================================
 * - Fixed broken PCM stream buffer loop in generateTTSAudio() and speakText()
 *   (flawed millis() arithmetic caused premature stream cutoff / corrupt WAV data)
 * - Fixed playWAVSpeaker() to read sample rate from WAV header and reinitialize
 *   I2S speaker at the correct rate before playback (24kHz for TTS, 16kHz for
 *   recorded audio) -- eliminates pitch/speed distortion on speaker output
 * - Web browser playback now receives a clean well-formed WAV file
 *
 * v1.2.0 UPDATE: WAV TTS OUTPUT
 * =================================================
 * - TTS responses now saved and played as WAV (PCM) instead of MP3
 * - OpenAI TTS API requested with response_format "pcm" (raw 16-bit PCM, 24kHz)
 * - WAV header written on the ESP32 before saving to SD card
 * - Enables native I2S speaker playback without an MP3 decoder
 * - Analysis export package saves audio as audio.wav instead of audio.mp3
 * - TTS MIME type updated to audio/wav throughout
 * - Version bump only; no changes to UNO sketch logic
 *
 * v1.1.0 UPDATE: REMOTE PIN CONTROL + TTS FEEDBACK
 * =================================================
 * - Merged v3.2.0 credential management (NVS storage, /settings page, DHCP display)
 * - Added GPIO pin command handlers for Arduino UNO remote control
 * - Serial command interface accepts PIN-based commands from UNO via Gravity connector
 * - TTS audio announcements for all remote-triggered actions
 * - Status LED feedback (GPIO 3) for visual confirmation
 *
 * SERIAL ARCHITECTURE:
 *   Serial      (USB CDC)     -> Arduino IDE Serial Monitor / debug output
 *   UNOSerial   (UART1 HW)   -> Gravity connector GPIO 44 (RX) / GPIO 43 (TX)
 *                               Connect to UNO SoftwareSerial pins 2 (RX) and 3 (TX)
 *                               Baud rate: 9600 (SoftwareSerial limit on UNO)
 *
 * REMOTE PIN ASSIGNMENTS (Arduino UNO side):
 * ------------------------------------------
 * Pin 4  -> Take Picture
 * Pin 5  -> Delete Last Picture
 * Pin 6  -> Record Audio
 * Pin 7  -> Delete Last Audio
 * Pin 8  -> Show Next Image  (TTS announces image number)
 * Pin 9  -> Show Prev Image  (TTS announces image number)
 * Pin 10 -> Play Next Audio  (TTS announces audio number)
 * Pin 11 -> Play Prev Audio  (TTS announces audio number)
 * Pin 12 -> Select Image for Export
 * Pin 13 -> Status Indicator LED
 *           1. Short flash = button pressed
 *           2. Long flash  = delete confirmed
 *           3. ON during recording/capture, OFF when complete
 * Pin A0 -> Select Audio for Export
 * Pin A1 -> Export (Send) to OpenAI
 * Pin A2 -> Play OpenAI Analysis (TTS replay)
 *
 * SERIAL WIRING (UNO SoftwareSerial pins 2,3 -> ESP32):
 * UNO Pin 2 (SW RX) -> ESP32 GPIO 43 (TX)
 * UNO Pin 3 (SW TX) -> ESP32 GPIO 44 (RX)
 * GND               -> GND
 *
 * Serial Protocol:
 * - Baud Rate: 115200
 * - Format: CMD:<command>|PARAM:<value>\n
 * - Response: OK:<data>\n or ERROR:<message>\n
 *
 * v1.0.0 Commands:
 * PING, STATUS, CAPTURE, RECORD:<sec>, LIST_IMAGES, LIST_AUDIO,
 * GET_LAST_IMAGE, GET_LAST_AUDIO, VERSION
 *
 * v1.1.0 New Commands (also triggered by UNO pins):
 * PIN:TAKE_PIC              -> Capture image + TTS confirmation
 * PIN:DELETE_PIC            -> Delete last image + TTS confirmation
 * PIN:RECORD_AUDIO:<sec>    -> Record audio + TTS confirmation
 * PIN:DELETE_AUDIO          -> Delete last audio + TTS confirmation
 * PIN:NEXT_IMAGE            -> Advance image pointer + TTS image number
 * PIN:PREV_IMAGE            -> Go back image pointer + TTS image number
 * PIN:NEXT_AUDIO            -> Advance audio pointer + TTS audio number
 * PIN:PREV_AUDIO            -> Go back audio pointer + TTS audio number
 * PIN:SELECT_IMAGE          -> Select current image for export + TTS confirm
 * PIN:SELECT_AUDIO          -> Select current audio for export + TTS confirm
 * PIN:EXPORT                -> Trigger OpenAI analysis on selected pair + TTS status
 * PIN:REPLAY_TTS            -> Replay last OpenAI TTS response
 */

#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "ESP_I2S.h"

// -- UNO Serial link ------------------------------------------
// Hardware UART1 on the Gravity connector pins.
// Must match espSerial.begin() baud rate in the UNO sketch.
// SoftwareSerial on a 16MHz UNO is unreliable above 9600.
#define UNO_RX_PIN  44   // ESP32 receives on GPIO 44 <- UNO pin 3 (TX)
#define UNO_TX_PIN  43   // ESP32 transmits on GPIO 43 -> UNO pin 2 (RX)
#define UNO_BAUD  9600
HardwareSerial UNOSerial(1);  // UART1
#include "mbedtls/base64.h"

// ============================================================================
// INLINE SETTINGS STORE (replaces credentials_manager.h -- v1.2.35)
// Lightweight NVS read/write with no captive portal or AP mode.
// Credentials are loaded from NVS on boot; DEFAULT_* constants are used
// if NVS is empty (first flash). /settings page saves back to NVS.
// ============================================================================
#include <Preferences.h>

class SettingsStore {
public:
  static void begin(const char* defaultSSID, const char* defaultPassword,
                    const char* defaultAPIKey, const char* defaultDeviceName) {
    prefs.begin("ai_camera", false);
    // Load from NVS; fall back to compiled-in defaults if not yet set
    wifi_ssid     = prefs.getString("wifi_ssid",   String(defaultSSID));
    wifi_password = prefs.getString("wifi_pass",   String(defaultPassword));
    openai_key    = prefs.getString("openai_key",  String(defaultAPIKey));
    device_name   = prefs.getString("device_name", String(defaultDeviceName));
    // If defaults were loaded (NVS empty), persist them now
    if (!prefs.isKey("wifi_ssid")) save(wifi_ssid, wifi_password, openai_key, device_name);
    Serial.printf("[Settings] SSID: %s | Key: %d chars\n",
                  wifi_ssid.c_str(), openai_key.length());
  }
  static void save(String ssid, String password, String apiKey, String deviceName) {
    prefs.putString("wifi_ssid",   ssid);
    prefs.putString("wifi_pass",   password);
    prefs.putString("openai_key",  apiKey);
    prefs.putString("device_name", deviceName);
    wifi_ssid = ssid; wifi_password = password;
    openai_key = apiKey; device_name = deviceName;
    Serial.println("[Settings] Saved to NVS");
  }
  static String getWiFiSSID()     { return wifi_ssid; }
  static String getWiFiPassword() { return wifi_password; }
  static String getOpenAIKey()    { return openai_key; }
  static String getDeviceName()   { return device_name; }
private:
  static Preferences prefs;
  static String wifi_ssid, wifi_password, openai_key, device_name;
};
Preferences SettingsStore::prefs;
String SettingsStore::wifi_ssid;
String SettingsStore::wifi_password;
String SettingsStore::openai_key;
String SettingsStore::device_name;

// ============================================================================

// v3.2.0 Security and reliability headers
#include "ssl_validation.h"
#include "error_handling.h"

#define FIRMWARE_VERSION    "1.2.41"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// ============================================================================
// PIPELINE TIMING DIAGNOSTICS (v1.2.18 / v1.2.19)
// ============================================================================
struct PipelineTiming {
  // Timestamps (millis)
  uint32_t t0_taskStart      = 0;
  uint32_t t1_imgOpened      = 0;
  uint32_t t2_b64Done        = 0;
  uint32_t t3_visionPost     = 0;
  uint32_t t4_visionResponse = 0;
  uint32_t t5_visionParsed   = 0;
  uint32_t t6_ttsPost        = 0;
  uint32_t t7_ttsFB          = 0;   // first byte from TTS stream
  uint32_t t8_ttsStreamDone  = 0;
  uint32_t t9_sdWriteDone    = 0;
  uint32_t t10_flagSet       = 0;
  uint32_t t11_playStart     = 0;
  uint32_t t12_playDone      = 0;

  // File / memory metrics
  uint32_t imgFileBytes      = 0;
  uint32_t b64Chars          = 0;
  uint32_t audioFileBytes    = 0;
  uint32_t pcmBytesBuffered  = 0;
  uint32_t ttsWavBytes       = 0;
  uint32_t psramFreeStart    = 0;
  uint32_t psramFreeAfterBuf = 0;
  uint32_t heapFreeStart     = 0;
  int8_t   wifiRSSI          = 0;

  // Status
  bool     completed         = false;
  bool     isSynthetic       = false;  // true when run via /test/pipeline
  String   errorStage        = "";
  String   runID             = "";
  uint8_t  apiKeyLen         = 0;      // sanitized key length (0 = not set)

  // Derived helpers (call after run completes)
  uint32_t ms_b64()       const { return t1_imgOpened  ? t2_b64Done      - t1_imgOpened  : 0; }
  uint32_t ms_vision()    const { return t3_visionPost ? t4_visionResponse- t3_visionPost : 0; }
  uint32_t ms_ttsfb()     const { return t6_ttsPost    ? t7_ttsFB        - t6_ttsPost    : 0; }
  uint32_t ms_ttsstream() const { return t7_ttsFB      ? t8_ttsStreamDone- t7_ttsFB      : 0; }
  uint32_t ms_sdwrite()   const { return t8_ttsStreamDone ? t9_sdWriteDone-t8_ttsStreamDone:0;}
  uint32_t ms_uihold()    const { return t10_flagSet   ? t11_playStart   - t10_flagSet   : 0; }
  uint32_t ms_play()      const { return t11_playStart ? t12_playDone    - t11_playStart : 0; }
  uint32_t ms_total()     const { return t0_taskStart  ? (t12_playDone > 0 ? t12_playDone : millis()) - t0_taskStart : 0; }
};

// Stored result of the last completed (or failed) pipeline run
static PipelineTiming lastDiag;
static bool diagAvailable = false;

// Print full timing table to Serial Monitor
void printDiagTable(const PipelineTiming& d) {
  Serial.println("\n+==================================================+");
  Serial.println(  "|       PIPELINE TIMING DIAGNOSTICS v1.2.41        |");
  Serial.println(  "+==================================================+");
  Serial.printf(   "|  Run ID    : %-34s  |\n", d.runID.c_str());
  Serial.printf(   "|  Mode      : %-34s  |\n", d.isSynthetic ? "SYNTHETIC (test)" : "LIVE");
  Serial.printf(   "|  Status    : %-34s  |\n", d.completed ? "COMPLETE" : ("FAILED @ " + d.errorStage).c_str());
  Serial.println(  "+==================================================+");
  Serial.println(  "|  STAGE TIMINGS                                    |");
  Serial.printf(   "|  T0 Task accepted       : %6u ms (origin)      |\n", 0);
  Serial.printf(   "|  T1 Image opened        : %6u ms               |\n", d.t1_imgOpened  - d.t0_taskStart);
  Serial.printf(   "|  T2 Base64 done         : %6u ms (+%5u enc)   |\n", d.t2_b64Done    - d.t0_taskStart, d.ms_b64());
  Serial.printf(   "|  T3 Vision POST sent    : %6u ms               |\n", d.t3_visionPost - d.t0_taskStart);
  Serial.printf(   "|  T4 Vision response     : %6u ms (+%5u net)   |\n", d.t4_visionResponse-d.t0_taskStart, d.ms_vision());
  Serial.printf(   "|  T5 Vision parsed       : %6u ms               |\n", d.t5_visionParsed-d.t0_taskStart);
  Serial.printf(   "|  T6 TTS POST sent       : %6u ms               |\n", d.t6_ttsPost    - d.t0_taskStart);
  Serial.printf(   "|  T7 TTS first byte      : %6u ms (+%5u TTFB)  |\n", d.t7_ttsFB      - d.t0_taskStart, d.ms_ttsfb());
  Serial.printf(   "|  T8 TTS stream done     : %6u ms (+%5u strm)  |\n", d.t8_ttsStreamDone-d.t0_taskStart, d.ms_ttsstream());
  Serial.printf(   "|  T9 SD write done       : %6u ms (+%5u sd)    |\n", d.t9_sdWriteDone - d.t0_taskStart, d.ms_sdwrite());
  Serial.printf(   "|  T10 pendingPlay set    : %6u ms               |\n", d.t10_flagSet   - d.t0_taskStart);
  Serial.printf(   "|  T11 Playback started   : %6u ms (+%5u hold)  |\n", d.t11_playStart  - d.t0_taskStart, d.ms_uihold());
  Serial.printf(   "|  T12 Playback done      : %6u ms (+%5u play)  |\n", d.t12_playDone   - d.t0_taskStart, d.ms_play());
  Serial.println(  "+==================================================+");
  Serial.println(  "|  FILE / MEMORY METRICS                            |");
  Serial.printf(   "|  Image file    : %8u bytes                    |\n", d.imgFileBytes);
  Serial.printf(   "|  Base64 output : %8u chars                    |\n", d.b64Chars);
  Serial.printf(   "|  Audio input   : %8u bytes                    |\n", d.audioFileBytes);
  Serial.printf(   "|  PCM buffered  : %8u bytes (PSRAM)            |\n", d.pcmBytesBuffered);
  Serial.printf(   "|  TTS WAV file  : %8u bytes (SD)               |\n", d.ttsWavBytes);
  Serial.printf(   "|  Heap free     : %8u bytes (at task start)    |\n", d.heapFreeStart);
  Serial.printf(   "|  PSRAM before  : %8u bytes                    |\n", d.psramFreeStart);
  Serial.printf(   "|  PSRAM after   : %8u bytes                    |\n", d.psramFreeAfterBuf);
  Serial.printf(   "|  WiFi RSSI     : %8d dBm                      |\n", (int)d.wifiRSSI);
  Serial.printf(   "|  API Key len   : %8d chars (sanitized)         |\n", (int)d.apiKeyLen);
  Serial.println(  "+==================================================+");
  Serial.printf(   "|  TOTAL WALL TIME: %6u ms (%.1f seconds)       |\n", d.ms_total(), d.ms_total()/1000.0f);
  Serial.println(  "+==================================================+\n");
}

// ============================================================================
// USER CONFIGURATION - Edit these values before uploading
// ============================================================================
// On first flash these values are saved to NVS and used for all connections.
// After first boot, update credentials via the /settings page in the web UI
// without reflashing. Values here are only used when NVS is empty.
// SECURITY: Never commit real credentials to version control.

#define DEFAULT_WIFI_SSID       "YourSSID"                // Your WiFi network name
#define DEFAULT_WIFI_PASSWORD   "YourPassword"            // Your WiFi password
#define DEFAULT_OPENAI_API_KEY  "sk-..."                  // Your OpenAI API key
#define DEFAULT_DEVICE_NAME     "AI-Camera-Agent"         // Friendly device name

// ============================================================================

const char* OPENAI_WHISPER_URL = "https://api.openai.com/v1/audio/transcriptions";
const char* OPENAI_CHAT_URL    = "https://api.openai.com/v1/chat/completions";
const char* OPENAI_TTS_URL     = "https://api.openai.com/v1/audio/speech";

const char* OPENAI_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n" \
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n" \
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n" \
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n" \
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n" \
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n" \
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n" \
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n" \
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n" \
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n" \
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n" \
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n" \
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n" \
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n" \
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n" \
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n" \
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n" \
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n" \
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n" \
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n" \
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n" \
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n" \
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n" \
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n" \
"-----END CERTIFICATE-----\n";

WebServer server(80);

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    5
#define SIOD_GPIO_NUM    8
#define SIOC_GPIO_NUM    9
#define Y9_GPIO_NUM      4
#define Y8_GPIO_NUM      6
#define Y7_GPIO_NUM      7
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     17
#define Y4_GPIO_NUM     21
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM     16
#define VSYNC_GPIO_NUM   1
#define HREF_GPIO_NUM    2
#define PCLK_GPIO_NUM   15
#define SD_MMC_CMD      11
#define SD_MMC_CLK      12
#define SD_MMC_D0       13
#define PDM_DATA_PIN  GPIO_NUM_39
#define PDM_CLOCK_PIN GPIO_NUM_38
#define I2S_BCLK        45
#define I2S_LRC         46
#define I2S_DOUT        42
#define I2S_SD_PIN      41

// Onboard status LED (GPIO 3 per board schematic)
#define STATUS_LED_PIN   3

#define SAMPLE_RATE     16000  // PDM mic recording rate + recorded audio playback rate
#define TTS_SAMPLE_RATE 24000  // OpenAI TTS WAV native output rate (must match for speaker)
#define AMPLIFICATION      15  // Mic recording amplification (PDM input gain x15)

// TTS playback attenuation -- right-shift applied to each sample before I2S output.
// The MAX98357A default gain is 9dB (GAIN pin floating). At full digital scale
// (0 dBFS) the amplifier output stage clips, causing audible distortion/noise.
// Attenuation values: 0=none (clips), 1=-3dB (borderline), 2=-6dB (default, clean)
#define TTS_PLAY_ATTENUATION  1   // -6dB: peak I2S = 16383, clean through MAX98357A

// ============================================================================
// STATE VARIABLES
// ============================================================================
int imageCount = 0;
int audioCount = 0;
int ttsCount   = 0;
bool streamingEnabled = true;
volatile bool recordingInProgress = false;
volatile bool ttsInProgress       = false;

// Progress tracking for OpenAI analysis
volatile bool analysisInProgress     = false;
String analysisProgressStage         = "";
String analysisProgressDetail        = "";
int    analysisProgressPercent       = 0;

// Store completed analysis results
bool   analysisResultReady   = false;
String analysisResultText    = "";
String analysisResultTTSFile = "";
bool   analysisResultSuccess = false;

// Deferred TTS playback -- set by runOpenAIAnalysis(), consumed by loop()
// Decouples speaker playback from the analysis pipeline so the web server
// can serve the result text to the browser before audio starts.
// (v1.2.36: pendingTTSPlay removed -- analysis TTS plays via browser, not speaker)

// Current analysis request
String currentImagePath    = "";
String currentAudioPath    = "";
String currentAnalysisID   = "";

// Serial command interface
String serialCommandBuffer = "";
bool   serialCommandReady  = false;
String lastCapturedImage   = "";
String lastRecordedAudio   = "";

// -- Serial Diagnostics (v1.1.0) ----------------------------------------------
// Tracks activity visible in the web Serial Monitor panel
unsigned long serialTotalRx       = 0;   // Total bytes received on serial
unsigned long serialTotalTx       = 0;   // Total bytes sent on serial
unsigned long serialCmdCount      = 0;   // Commands successfully processed
unsigned long serialErrCount      = 0;   // ERROR responses sent
unsigned long serialLastActivityMs= 0;   // millis() of last RX byte
bool          serialPortActive    = false; // true if activity in last 5 s
unsigned long serialHeartbeatMs   = 0;   // millis() of last heartbeat sent

// v1.2.41 UNO sync -- send ONLINE announcement at end of setup() then
// repeat every UNO_SYNC_INTERVAL_MS for UNO_SYNC_DURATION_MS so the UNO
// can detect the ESP32 coming up regardless of which device booted first.
#define UNO_SYNC_INTERVAL_MS  3000   // repeat ONLINE every 3 s
#define UNO_SYNC_DURATION_MS  30000  // keep repeating for 30 s after boot
unsigned long unoSyncLastMs   = 0;   // millis() of last ONLINE sent
bool          unoSyncComplete = false; // true once 30 s window has passed

// Ring buffer -- last 20 log lines shown in the web Serial Monitor
#define SERIAL_LOG_SIZE 20
String serialLog[SERIAL_LOG_SIZE];
int    serialLogHead = 0;   // next write position (circular)
int    serialLogCount= 0;   // how many entries filled so far

void serialLogPush(String line) {
  // Trim to keep panel readable
  if (line.length() > 80) line = line.substring(0, 77) + "...";
  serialLog[serialLogHead] = line;
  serialLogHead = (serialLogHead + 1) % SERIAL_LOG_SIZE;
  if (serialLogCount < SERIAL_LOG_SIZE) serialLogCount++;
}

// v1.1.0 Remote control state
// Browseable file indices for remote navigation
int currentImageIndex = -1;   // Index into sorted image list
int currentAudioIndex = -1;   // Index into sorted audio list
String imageForExport = "";   // Currently selected image for export
String audioForExport = "";   // Currently selected audio for export
String lastTTSFile    = "";   // Last generated TTS response file
bool   exportPending  = false;// Export pair has been staged

// Sorted file caches (refreshed on capture/record/delete)
std::vector<String> imageFileList;
std::vector<String> audioFileList;
std::vector<String> ttsFileList;

I2SClass i2s_mic;
I2SClass i2s_spk;

// ============================================================================
// STATUS LED HELPERS
// ============================================================================
void ledShortFlash() {
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(80);
  digitalWrite(STATUS_LED_PIN, LOW);
}

void ledLongFlash() {
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(600);
  digitalWrite(STATUS_LED_PIN, LOW);
}

void ledOn()  { digitalWrite(STATUS_LED_PIN, HIGH); }
void ledOff() { digitalWrite(STATUS_LED_PIN, LOW);  }

// ============================================================================
// FILE LIST CACHE HELPERS
// ============================================================================
void refreshImageList() {
  imageFileList.clear();
  File root = SD_MMC.open("/images");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) imageFileList.push_back(String(f.name()));
    f = root.openNextFile();
  }
  root.close();
  // Keep index valid
  if (imageFileList.empty()) {
    currentImageIndex = -1;
  } else if (currentImageIndex >= (int)imageFileList.size()) {
    currentImageIndex = imageFileList.size() - 1;
  }
}

void refreshAudioList() {
  audioFileList.clear();
  File root = SD_MMC.open("/audio");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) audioFileList.push_back(String(f.name()));
    f = root.openNextFile();
  }
  root.close();
  if (audioFileList.empty()) {
    currentAudioIndex = -1;
  } else if (currentAudioIndex >= (int)audioFileList.size()) {
    currentAudioIndex = audioFileList.size() - 1;
  }
}

void refreshTTSList() {
  ttsFileList.clear();
  File root = SD_MMC.open("/tts");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) ttsFileList.push_back(String(f.name()));
    f = root.openNextFile();
  }
  root.close();
}

// ============================================================================
// QUICK TTS ANNOUNCEMENT (non-blocking for short phrases)
// Generates speech via OpenAI TTS, saves to SD, plays through speaker
// Returns filename of generated audio, or "" on failure
// ============================================================================
String speakText(String text) {
  if (text.length() == 0) return "";
  if (!WiFi.isConnected()) {
    Serial.println("[TTS] WiFi not connected, skipping announcement");
    return "";
  }

  Serial.println("[TTS-Announce] " + text);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20);

  HTTPClient https;
  https.begin(client, OPENAI_TTS_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + sanitizeAPIKey(SettingsStore::getOpenAIKey()));
  https.setTimeout(20000);
  https.setReuse(false);

  // OpenAI TTS returns 24kHz raw PCM -- fed directly to FIR downsampler (v1.2.33 revert)
  DynamicJsonDocument doc(1024);
  doc["model"]           = "tts-1";
  doc["input"]           = text;
  doc["voice"]           = "nova";
  doc["response_format"] = "pcm";   // Raw 16-bit signed PCM, 24kHz mono -- no header

  String reqBody;
  serializeJson(doc, reqBody);

  int httpCode = https.POST(reqBody);
  String filename = "";

  if (httpCode == 200) {
    WiFiClient* stream = https.getStreamPtr();
    ttsCount++;
    filename = "ANN_" + String(ttsCount) + ".wav";

    // --- PSRAM buffer -- eliminates network stall silence gaps (see patch note) ---
    const size_t MAX_PCM_BYTES_ANN = 45 * SAMPLE_RATE * 2;
    uint8_t* pcmBuf = (uint8_t*)ps_malloc(MAX_PCM_BYTES_ANN);
    bool usePsram = (pcmBuf != nullptr);
    if (!usePsram) Serial.println("[TTS] ps_malloc failed, using direct SD write");

    File ttsFile = SD_MMC.open("/tts/" + filename, FILE_WRITE);
    if (ttsFile) {
      static uint8_t rawBuf[768];    // 24kHz receive buffer (384 samples)
      static int16_t dstBuf[512];   // 16kHz output buffer after FIR+decimation
      firReset();                    // reset FIR state for this new stream
      size_t pcmBytes = 0;
      bool done = false;
      unsigned long lastDataMs = millis();

      if (!usePsram) {
        uint32_t zero=0; uint16_t af=1,nc=1,ba=2,bp=16; uint32_t sc=16;
        uint32_t br=SAMPLE_RATE*2; uint32_t wavSR=SAMPLE_RATE;
        ttsFile.write((uint8_t*)"RIFF",4); ttsFile.write((uint8_t*)&zero,4);
        ttsFile.write((uint8_t*)"WAVE",4); ttsFile.write((uint8_t*)"fmt ",4);
        ttsFile.write((uint8_t*)&sc,4);   ttsFile.write((uint8_t*)&af,2);
        ttsFile.write((uint8_t*)&nc,2);   ttsFile.write((uint8_t*)&wavSR,4);
        ttsFile.write((uint8_t*)&br,4);   ttsFile.write((uint8_t*)&ba,2);
        ttsFile.write((uint8_t*)&bp,2);   ttsFile.write((uint8_t*)"data",4);
        ttsFile.write((uint8_t*)&zero,4);
      }

      while (!done && https.connected() && (millis() - lastDataMs < 5000)) {
        String sizeLine = "";
        unsigned long lineStart = millis();
        while (millis() - lineStart < 2000) {
          if (stream->available()) {
            char c = stream->read();
            if (c == '\n') break;
            if (c != '\r') sizeLine += c;
          } else { vTaskDelay(1); }
        }
        if (sizeLine.length() == 0) { vTaskDelay(1); continue; }
        size_t chunkLen = (size_t)strtoul(sizeLine.c_str(), nullptr, 16);
        if (chunkLen == 0) { done = true; break; }
        lastDataMs = millis();

        size_t remaining = chunkLen;
        while (remaining > 0 && https.connected() && (millis() - lastDataMs < 5000)) {
          size_t toRead = min(remaining, sizeof(rawBuf));
          int len = stream->readBytes(rawBuf, toRead);
          if (len > 0) {
            remaining -= len;
            lastDataMs = millis();
            if (len >= 2) {
              len -= (len % 2);
              size_t nSrc = len / 2;
              size_t nDst = downsample24to16((int16_t*)rawBuf, nSrc,
                                             dstBuf, sizeof(dstBuf)/2);
              if (nDst > 0) {
                size_t nBytes = nDst * 2;
                if (usePsram && pcmBytes + nBytes <= MAX_PCM_BYTES_ANN) {
                  memcpy(pcmBuf + pcmBytes, dstBuf, nBytes);
                } else {
                  ttsFile.write((uint8_t*)dstBuf, nBytes);
                }
                pcmBytes += nBytes;
              }
            }
          } else { vTaskDelay(1); }
        }
        while (stream->available() < 2 && millis() - lastDataMs < 1000) vTaskDelay(1);
        if (stream->available() >= 2) { stream->read(); stream->read(); }
        vTaskDelay(1);
      }

      if (usePsram && pcmBytes > 0) {
        uint32_t dataSize = pcmBytes; uint32_t riffSize = dataSize + 36;
        uint16_t af=1,nc=1,ba=2,bp=16; uint32_t sc=16;
        uint32_t br=SAMPLE_RATE*2; uint32_t wavSR=SAMPLE_RATE;
        ttsFile.write((uint8_t*)"RIFF",4); ttsFile.write((uint8_t*)&riffSize,4);
        ttsFile.write((uint8_t*)"WAVE",4); ttsFile.write((uint8_t*)"fmt ",4);
        ttsFile.write((uint8_t*)&sc,4);   ttsFile.write((uint8_t*)&af,2);
        ttsFile.write((uint8_t*)&nc,2);   ttsFile.write((uint8_t*)&wavSR,4);
        ttsFile.write((uint8_t*)&br,4);   ttsFile.write((uint8_t*)&ba,2);
        ttsFile.write((uint8_t*)&bp,2);   ttsFile.write((uint8_t*)"data",4);
        ttsFile.write((uint8_t*)&dataSize,4);
        ttsFile.write(pcmBuf, pcmBytes);
      } else if (!usePsram) {
        uint32_t dataSize = pcmBytes; uint32_t riffSize = dataSize + 36;
        ttsFile.seek(4);  ttsFile.write((uint8_t*)&riffSize, 4);
        ttsFile.seek(40); ttsFile.write((uint8_t*)&dataSize, 4);
      }
      ttsFile.close();
      if (pcmBuf) { free(pcmBuf); pcmBuf = nullptr; }

      if (pcmBytes == 0) {
        SD_MMC.remove("/tts/" + filename);
        filename = "";
      } else {
        Serial.printf("[TTS-Announce] WAV saved: %s (%d bytes at 16kHz)\n", filename.c_str(), pcmBytes);
        refreshTTSList();
      }
    } else {
      if (pcmBuf) { free(pcmBuf); pcmBuf = nullptr; }
      filename = "";
    }
  }
  https.end();

  if (filename.length() > 0) {
    playWAVSpeaker("/tts/" + filename);
  } else {
    Serial.println("[TTS-Announce] TTS failed for: " + text);
  }

  return filename;
}

// Flush I2S DMA with silence to prevent stale-buffer noise at start/end of playback.
// bytes = number of bytes of silence to write (stereo 16-bit frames).
// Writes continuously without mid-loop yield to prevent DMA underrun gaps.
void i2sFlushZeros(uint32_t bytes) {
  static uint8_t silenceBuf[512] = {0};   // static: off stack, always zero
  uint32_t remaining = bytes;
  while (remaining > 0) {
    uint32_t n = min((uint32_t)sizeof(silenceBuf), remaining);
    i2s_spk.write(silenceBuf, n);   // write silence to DMA -- no yield between chunks
    remaining -= n;
  }
  vTaskDelay(1);   // yield once after full flush, not between every chunk
}

// ============================================================================
// FIR ANTI-ALIAS DOWNSAMPLER  24kHz -> 16kHz  (v1.2.24)
// ============================================================================
// v1.2.23 used a 31-tap Hann-windowed FIR but had two bugs:
//   1. Coefficients summed to 9827 (not 32768) -- caused -10.5 dB volume loss
//   2. Hann window only provides ~28 dB stopband rejection
//
// v1.2.24 replaces it with a 63-tap Kaiser-windowed (beta=6) FIR:
//   Cutoff:    7,200 Hz  (below the 8,000 Hz Nyquist of 16kHz output)
//   Window:    Kaiser beta=6  (~69 dB stopband rejection)
//   Taps:      63        (symmetric, linear phase -- no pitch distortion)
//   DC gain:   unity     (sum=32769 ~= 32768, error < 0.01 dB)
//
// Coefficients generated in Python:
//   sinc LPF normalised to sum=1.0, scaled to Q15 (x32768), rounded to int16.
// Filter state (62 samples) preserved in static array across HTTP chunks.
// After filtering, 3:2 decimation: emit 2 samples, skip 1, repeat.
// ============================================================================

#define FIR_TAPS 63

// 63-tap Kaiser (beta=6) windowed-sinc, Fc=7200Hz, Fs=24000Hz.
// Sum = 32769 -> DC gain = 0.9999x (-0.00 dB). Stopband >= 69 dB.
static const int16_t FIR_H[FIR_TAPS] = {
     5,   0, -13,  11,  16, -34,   0,  58, -46, -57,
   114,   0,-168, 125, 148,-284,   0, 394,-285,-334,
   632,   0,-871, 637, 760,-1491,  0,2369,-1992,-3030,
  9891,19659,9891,-3030,-1992,2369, 0,-1491, 760, 637,
  -871,  0, 632,-334,-285, 394,   0,-284, 148, 125,
  -168,  0, 114, -57, -46,  58,   0, -34,  16,  11,
   -13,  0,   5
};

// Filter state: last (FIR_TAPS-1) = 62 input samples, preserved across chunks.
static int16_t firState[FIR_TAPS - 1];
static bool    firStateInit = false;

// HF shelf state -- applied to FIR output before WAV write (v1.2.30).
// Reduces harshness and eliminates clipping at the source.
// alpha=0.5, wet=0.7: -0.3dB@500Hz, -2.5dB@2kHz, -4.5dB@4kHz, -5.5dB@8kHz
// Preserved across chunks; reset by firReset().
static int32_t shelfLpPrev = 0;
static const int32_t SHELF_ALPHA_Q15 = 16384;  // 0.5 x 32768
static const int32_t SHELF_1MA_Q15   = 16384;  // 0.5 x 32768
static const int32_t SHELF_WET_Q15   = 22938;  // 0.7 x 32768
static const int32_t SHELF_DRY_Q15   =  9830;  // 0.3 x 32768

// Reset all filter state -- call once before the first chunk of a new TTS stream.
void firReset() {
  memset(firState, 0, sizeof(firState));
  shelfLpPrev = 0;
  firStateInit = true;
}

// Downsample 24kHz mono PCM to 16kHz using FIR anti-alias filter + 3:2 decimation.
// HF shelf (alpha=0.5, wet=0.7) applied to each output sample before write --
// reduces harshness and prevents int16 clipping on loud consonants.
// src: input 24kHz samples  srcLen: number of input int16 samples
// dst: output 16kHz samples dstMaxLen: max output samples
// Returns number of output samples written.
// Call firReset() once before the first chunk of a new TTS stream.
size_t downsample24to16(const int16_t* src, size_t srcLen,
                         int16_t* dst, size_t dstMaxLen) {
  if (!firStateInit) firReset();
  if (srcLen == 0) return 0;

  // Prepend history so filter runs continuously across HTTP chunk boundaries.
  // Max rawBuf is 384 samples; histLen=62; total buf <= 446 int16 = 892 bytes.
  const size_t histLen = FIR_TAPS - 1;  // 62
  const size_t maxBuf  = histLen + 512; // 574 -- safe stack allocation
  int16_t buf[574];
  size_t copyLen = min(srcLen, maxBuf - histLen);
  memcpy(buf, firState, histLen * 2);
  memcpy(buf + histLen, src, copyLen * 2);
  size_t inputSamples = histLen + copyLen;

  // 3:2 decimation: phase 0=emit, 1=emit, 2=skip, repeat
  size_t dstLen = 0;
  int    phase  = 0;

  for (size_t i = histLen; i < inputSamples && dstLen < dstMaxLen; i++) {
    // Causal FIR: convolve buf[i - (TAPS-1) .. i] with FIR_H
    // MUST use int64_t: max accumulator = 63 x 32767 x 19659 = 40.5 billion,
    // which overflows int32 (max 2.1 billion) by 18.9x on loud speech frames.
    int64_t acc = 0;
    for (int k = 0; k < FIR_TAPS; k++) {
      acc += (int64_t)buf[i - (FIR_TAPS - 1 - k)] * (int64_t)FIR_H[k];
    }
    int32_t fir_out = (int32_t)(acc >> 15);  // Q15 -> int32 (may exceed int16)

    // HF shelf: lp blend softens >2kHz before constrain() -- eliminates clipping
    // lp[n] = alpha*lp[n-1] + (1-alpha)*fir_out
    // out   = dry*fir_out + wet*lp[n]
    int32_t lp  = ((SHELF_ALPHA_Q15 * shelfLpPrev) + (SHELF_1MA_Q15 * fir_out)) >> 15;
    shelfLpPrev = lp;
    int32_t shelf_out = ((SHELF_DRY_Q15 * fir_out) + (SHELF_WET_Q15 * lp)) >> 15;

    if (phase != 2) dst[dstLen++] = (int16_t)constrain(shelf_out, -32768, 32767);
    phase = (phase + 1) % 3;
  }

  // Save last 62 samples as history for next chunk
  size_t histStart = (inputSamples >= histLen) ? (inputSamples - histLen) : 0;
  memcpy(firState, buf + histStart, histLen * 2);

  return dstLen;
}

// Legacy stub -- no longer called (v1.2.23+)
size_t downsampleLinear(const int16_t* src, size_t srcLen,
                         int16_t* dst, size_t dstMaxLen,
                         uint32_t srcRate, uint32_t dstRate,
                         uint32_t& posCarry) {
  (void)srcRate; (void)dstRate; (void)posCarry;
  return downsample24to16(src, srcLen, dst, dstMaxLen);
}


// Play a WAV file through the I2S speaker (blocking).
// All WAV files are expected to be at SAMPLE_RATE (16kHz) mono after v1.2.5.
// I2S is never reinitialized -- it stays at 16kHz stereo throughout.
// PDM mic is stopped before playback and restarted after (v1.2.31 crosstalk fix).
void playWAVSpeaker(String filePath) {
  // Stop PDM mic to eliminate ~1MHz PDM clock coupling into I2S speaker lines.
  // On the compact DFR1154 PCB, GPIO38 (PDM CLK) runs adjacent to GPIO42/45/46.
  i2s_mic.end();

  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) return;

  // DC-blocking IIR high-pass filter state (v1.2.27)
  // y[n] = x[n] - x[n-1] + HP_ALPHA * y[n-1]
  // HP_ALPHA = 0.9999 -> cutoff ~= 1.6Hz -- removes DC, preserves all speech.
  // Implemented in fixed-point: alpha scaled to Q15 (32767 * 0.9999 = 32764).
  // State reset to zero at start of each file to prevent inter-file artifacts.
  static const int32_t HP_ALPHA_Q15 = 32764;  // 0.9999 x 32768
  int32_t hp_x_prev = 0;   // previous input sample (integer)
  int32_t hp_y_prev = 0;   // previous output sample (Q15 scaled)

  // Skip WAV header (44 bytes standard)
  size_t fileSize = file.size();
  if (fileSize <= 44) { file.close(); return; }
  file.seek(44);
  size_t pcmSize = fileSize - 44;

  // -- PSRAM FAST-PATH --------------------------------------------------------
  // Load entire PCM into PSRAM before starting I2S. Eliminates SD read latency
  // (5-50ms per chunk) during playback -- the primary cause of DMA starvation
  // and the noise->clear->noise pattern on large files (v1.2.21 fix).
  // Falls back to SD streaming if PSRAM is insufficient.
  size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint8_t* pcmBuf  = nullptr;
  bool     usingPSRAM = false;

  if (psramFree > pcmSize + 65536) {   // require 64KB headroom beyond file
    pcmBuf = (uint8_t*)heap_caps_malloc(pcmSize, MALLOC_CAP_SPIRAM);
    if (pcmBuf) {
      size_t bytesRead = file.read(pcmBuf, pcmSize);
      file.close();
      if (bytesRead == pcmSize) {
        usingPSRAM = true;
        Serial.printf("[Audio] PSRAM fast-path: %u bytes loaded, playing...\n", pcmSize);
      } else {
        // Partial read -- fall through to SD path
        heap_caps_free(pcmBuf);
        pcmBuf = nullptr;
        // Re-open and seek since file is now closed
        file = SD_MMC.open(filePath, FILE_READ);
        if (!file) return;
        file.seek(44);
      }
    }
  }

  static uint8_t mono[4096];
  static uint8_t stereo[8192];

  if (usingPSRAM) {
    // -- PSRAM playback: feed I2S directly from RAM buffer -----------------
    size_t   offset     = 0;
    uint8_t  yieldCount = 0;
    while (offset < pcmSize) {
      size_t bytesRead = min((size_t)sizeof(mono), pcmSize - offset);
      memcpy(mono, pcmBuf + offset, bytesRead);
      offset += bytesRead;

      if (bytesRead & 1) bytesRead--;   // keep 16-bit alignment
      int16_t* ms = (int16_t*)mono;
      int16_t* ss = (int16_t*)stereo;
      size_t   n  = bytesRead / 2;

      // DC-blocking HP filter + attenuation + mono->stereo expand
      // (HF shelf moved to downsample24to16 in v1.2.30 -- applied at WAV write time)
      for (size_t i = 0; i < n; i++) {
        int32_t x = (int32_t)ms[i];
        // DC-blocking high-pass
        int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
        hp_x_prev = x;
        hp_y_prev = y;
        // Attenuate and clamp
        int16_t s = (int16_t)constrain(y >> TTS_PLAY_ATTENUATION, -32768, 32767);
        // Fade-out on last chunk
        if (offset >= pcmSize) {
          s = (int16_t)((int32_t)s * (int32_t)(n - i) / (int32_t)n);
        }
        ss[i * 2]     = s;
        ss[i * 2 + 1] = s;
      }
      i2s_spk.write(stereo, n * 4);   // blocks internally until DMA accepts data

      // taskYIELD: returns immediately if no higher-priority task is ready.
      // Unlike vTaskDelay(1), does NOT sleep for a full tick -- DMA stays fed.
      if (++yieldCount >= 32) { yieldCount = 0; taskYIELD(); }
    }
    heap_caps_free(pcmBuf);

  } else {
    // -- SD fallback: stream from SD card ----------------------------------
    // Used only when PSRAM is too full to hold the whole file.
    // Yield every 32 chunks (was 8 in v1.2.20) to reduce starvation risk.
    Serial.printf("[Audio] SD fallback path: %u bytes, streaming...\n", pcmSize);
    uint8_t chunkCount = 0;
    while (file.available()) {
      size_t bytesRead = file.read(mono, sizeof(mono));
      if (bytesRead > 1) {
        if (bytesRead & 1) bytesRead--;
        int16_t* ms = (int16_t*)mono;
        int16_t* ss = (int16_t*)stereo;
        size_t n = bytesRead / 2;

        // DC-blocking HP filter + attenuation + mono->stereo expand
        // (HF shelf moved to downsample24to16 in v1.2.30)
        for (size_t i = 0; i < n; i++) {
          int32_t x = (int32_t)ms[i];
          int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
          hp_x_prev = x;
          hp_y_prev = y;
          int16_t s = (int16_t)constrain(y >> TTS_PLAY_ATTENUATION, -32768, 32767);
          if (!file.available()) {
            s = (int16_t)((int32_t)s * (int32_t)(n - i) / (int32_t)n);
          }
          ss[i * 2]     = s;
          ss[i * 2 + 1] = s;
        }
        i2s_spk.write(stereo, n * 4);
      }
      if (++chunkCount >= 32) { chunkCount = 0; taskYIELD(); }
    }
    file.close();
  }

  // Post-flush: hold DMA at silence so MAX98357A mute circuit engages cleanly
  i2sFlushZeros((SAMPLE_RATE / 20) * 4);   // ~50ms silence

  // Restart PDM mic now that speaker playback is complete.
  // Allow 20ms for PDM clock to stabilise before any recording is triggered.
  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[Audio] WARNING: PDM mic restart failed after playback");
  }
  delay(20);  // PDM settle time
}

// ============================================================================
// BASE64 ENCODING
// ============================================================================
String base64EncodeFileOptimized(String filePath, PipelineTiming* diag = nullptr) {
  Serial.println("=== Base64 Encoding ===");
  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) { Serial.println("ERROR: Failed to open file"); return ""; }

  size_t fileSize = file.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  if (diag) { diag->imgFileBytes = fileSize; diag->t1_imgOpened = millis(); }

  size_t base64Size = ((fileSize + 2) / 3) * 4;
  String result = "";
  result.reserve(base64Size + 100);

  const size_t chunkSize = 3000;
  uint8_t* inputBuffer  = (uint8_t*)malloc(chunkSize);
  uint8_t* outputBuffer = (uint8_t*)malloc(chunkSize * 2);

  if (!inputBuffer || !outputBuffer) {
    if (inputBuffer) free(inputBuffer);
    if (outputBuffer) free(outputBuffer);
    file.close();
    return "";
  }

  size_t totalRead = 0;
  while (file.available()) {
    size_t bytesRead = file.read(inputBuffer, chunkSize);
    if (bytesRead > 0) {
      size_t outLen = 0;
      int ret = mbedtls_base64_encode(outputBuffer, chunkSize * 2, &outLen, inputBuffer, bytesRead);
      if (ret == 0) {
        for (size_t i = 0; i < outLen; i++) result += (char)outputBuffer[i];
        totalRead += bytesRead;
        if (totalRead % 30000 == 0) { Serial.printf("Encoded: %d bytes\n", totalRead); yield(); }
      } else { break; }
    }
  }

  file.close();
  free(inputBuffer);
  free(outputBuffer);
  Serial.printf("Encoding complete: %d -> %d chars\n", totalRead, result.length());
  if (diag) { diag->b64Chars = result.length(); diag->t2_b64Done = millis(); }
  return result;
}

// ============================================================================
// OPENAI API CALLS
// ============================================================================
String getHttpErrorString(int code) {
  switch(code) {
    case HTTPC_ERROR_CONNECTION_REFUSED:   return "Connection refused";
    case HTTPC_ERROR_SEND_HEADER_FAILED:  return "Send header failed";
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "Send payload failed";
    case HTTPC_ERROR_NOT_CONNECTED:       return "Not connected";
    case HTTPC_ERROR_CONNECTION_LOST:     return "Connection lost";
    case HTTPC_ERROR_NO_STREAM:           return "No stream";
    case HTTPC_ERROR_TOO_LESS_RAM:        return "Not enough RAM";
    case HTTPC_ERROR_READ_TIMEOUT:        return "Read timeout";
    default: return "Unknown error (" + String(code) + ")";
  }
}

// Sanitize the API key retrieved from NVS -- strips any whitespace or control
// characters that may have been appended during storage or copy-paste.
// Returns the cleaned key and logs a warning if anything was stripped.
String sanitizeAPIKey(const String& raw) {
  String key = raw;
  key.trim();  // removes leading/trailing whitespace including \r \n \t
  // Strip any remaining non-printable characters
  String clean = "";
  clean.reserve(key.length());
  for (int i = 0; i < (int)key.length(); i++) {
    char c = key[i];
    if (c >= 0x20 && c < 0x7F) clean += c;
  }
  if (clean.length() != raw.length()) {
    Serial.printf("[API] WARNING: Key sanitized %d->%d chars (stripped whitespace/control chars)\n",
                  raw.length(), clean.length());
  }
  if (clean.length() < 10 || !clean.startsWith("sk-")) {
    Serial.printf("[API] WARNING: Key looks invalid -- length=%d, prefix='%.6s'\n",
                  clean.length(), clean.c_str());
  } else {
    Serial.printf("[API] Key OK -- length=%d, prefix='%.7s', suffix='...%.4s'\n",
                  clean.length(), clean.c_str(), clean.c_str() + clean.length() - 4);
  }
  return clean;
}

String transcribeAudioWithWhisper(String audioFilePath) {
  Serial.println("\n=== OpenAI Whisper API ===");
  File audioFile = SD_MMC.open(audioFilePath, FILE_READ);
  if (!audioFile) return "Error: File not found";

  size_t fileSize = audioFile.size();
  uint8_t* audioData = (uint8_t*)ps_malloc(fileSize);
  if (!audioData) audioData = (uint8_t*)malloc(fileSize);
  if (!audioData) { audioFile.close(); return "Error: Memory allocation failed"; }

  audioFile.read(audioData, fileSize);
  audioFile.close();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);
  HTTPClient https;
  if (!https.begin(client, OPENAI_WHISPER_URL)) { free(audioData); return "Error: Connection setup failed"; }

  https.addHeader("Authorization", "Bearer " + sanitizeAPIKey(SettingsStore::getOpenAIKey()));
  https.setTimeout(30000);

  String boundary = "----ESP32" + String(millis());
  https.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  String bodyStart = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String bodyEnd   = "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n--" + boundary + "--\r\n";

  size_t totalSize = bodyStart.length() + fileSize + bodyEnd.length();
  uint8_t* requestBody = (uint8_t*)ps_malloc(totalSize);
  if (!requestBody) requestBody = (uint8_t*)malloc(totalSize);
  if (!requestBody) { free(audioData); https.end(); return "Error: Memory allocation failed"; }

  size_t offset = 0;
  memcpy(requestBody + offset, bodyStart.c_str(), bodyStart.length()); offset += bodyStart.length();
  memcpy(requestBody + offset, audioData, fileSize);                   offset += fileSize;
  memcpy(requestBody + offset, bodyEnd.c_str(), bodyEnd.length());

  int httpCode = https.POST(requestBody, totalSize);
  String transcription = "";

  if (httpCode == 200) {
    String response = https.getString();
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, response)) {
      transcription = doc["text"].as<String>();
    } else {
      transcription = "Error: JSON parse failed";
    }
  } else {
    transcription = "Error: Whisper returned " + String(httpCode);
  }

  free(audioData);
  free(requestBody);
  https.end();
  return transcription;
}

String analyzeImageWithGPT4Vision(String imageFilePath, String audioTranscription, PipelineTiming* diag = nullptr) {
  Serial.println("\n=== GPT-4 Vision ===");
  String imageBase64 = base64EncodeFileOptimized(imageFilePath, diag);
  if (imageBase64.length() == 0) return "Error: Base64 encoding failed";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, OPENAI_CHAT_URL)) { return "Error: Connection setup failed"; }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + sanitizeAPIKey(SettingsStore::getOpenAIKey()));
  https.setTimeout(60000);

  size_t docSize = imageBase64.length() + 4096;
  DynamicJsonDocument* doc = new DynamicJsonDocument(docSize);
  if (!doc) { imageBase64 = ""; https.end(); return "Error: JSON allocation failed"; }

  (*doc)["model"]      = "gpt-4o";
  (*doc)["max_tokens"] = 500;
  JsonArray messages   = doc->createNestedArray("messages");
  JsonObject userMsg   = messages.createNestedObject();
  userMsg["role"]      = "user";
  JsonArray content    = userMsg.createNestedArray("content");
  JsonObject textPart  = content.createNestedObject();
  textPart["type"]     = "text";
  String prompt        = "Analyze this image concisely. ";
  if (audioTranscription.length() > 0 && !audioTranscription.startsWith("Error")) {
    prompt += "Audio: \"" + audioTranscription + "\". Consider both.";
  }
  textPart["text"] = prompt;
  JsonObject imagePart = content.createNestedObject();
  imagePart["type"]    = "image_url";
  JsonObject imageUrl  = imagePart.createNestedObject("image_url");
  imageUrl["url"]      = "data:image/jpeg;base64," + imageBase64;

  String requestBody;
  serializeJson(*doc, requestBody);
  delete doc;
  imageBase64 = "";

  if (diag) diag->t3_visionPost = millis();
  int httpCode = https.POST(requestBody);
  requestBody = "";
  String analysis = "";
  if (diag) diag->t4_visionResponse = millis();

  if (httpCode == 200) {
    String response = https.getString();
    DynamicJsonDocument responseDoc(4096);
    if (!deserializeJson(responseDoc, response)) {
      analysis = responseDoc["choices"][0]["message"]["content"].as<String>();
      if (diag) diag->t5_visionParsed = millis();
    } else {
      analysis = "Error: JSON parse failed";
    }
  } else {
    String errBody = https.getString();
    Serial.printf("[Vision] HTTP error %d: %s\n", httpCode, errBody.substring(0, 200).c_str());
    analysis = "Error: GPT-4 returned " + String(httpCode);
  }

  https.end();
  return analysis;
}

// generateTTSAudio -- browser playback version (v1.2.36)
// Requests "wav" format directly from OpenAI TTS. The 24kHz WAV is saved
// to SD via chunked transfer decoder and served to the browser via /tts?file=
// No FIR downsampler or PSRAM buffer needed -- browser plays 24kHz natively.
String generateTTSAudio(String text, String analysisID, PipelineTiming* diag = nullptr) {
  Serial.println("\n=== OpenAI TTS (browser WAV) ===");
  ttsInProgress = true;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);
  HTTPClient https;
  https.begin(client, OPENAI_TTS_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + sanitizeAPIKey(SettingsStore::getOpenAIKey()));
  https.setTimeout(60000);
  https.setReuse(false);

  DynamicJsonDocument doc(4096);
  doc["model"]           = "tts-1";
  doc["input"]           = text;
  doc["voice"]           = "nova";
  doc["response_format"] = "wav";   // 24kHz WAV -- saved as-is, played by browser
  String requestBody;
  serializeJson(doc, requestBody);

  if (diag) diag->t6_ttsPost = millis();
  int httpCode = https.POST(requestBody);
  String ttsFilename = "";

  if (httpCode == 200) {
    WiFiClient* stream = https.getStreamPtr();
    ttsCount++;
    ttsFilename = "TTS_" + analysisID + ".wav";
    if (diag) diag->psramFreeStart = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    File ttsFile = SD_MMC.open("/tts/" + ttsFilename, FILE_WRITE);
    if (ttsFile) {
      uint8_t buf[512];
      size_t totalBytes = 0;
      bool done = false;
      unsigned long lastDataMs = millis();

      // Chunked transfer decoder -- identical to v3.2.4
      while (!done && https.connected() && (millis() - lastDataMs < 15000)) {
        String sizeLine = "";
        unsigned long lineStart = millis();
        while (millis() - lineStart < 2000) {
          if (stream->available()) {
            char c = stream->read();
            if (c == '\n') break;
            if (c != '\r') sizeLine += c;
          } else { vTaskDelay(1); }
        }
        if (sizeLine.length() == 0) { vTaskDelay(1); continue; }
        size_t chunkLen = (size_t)strtoul(sizeLine.c_str(), nullptr, 16);
        if (chunkLen == 0) { done = true; break; }
        lastDataMs = millis();
        if (diag && diag->t7_ttsFB == 0) diag->t7_ttsFB = millis();

        size_t remaining = chunkLen;
        while (remaining > 0 && https.connected() && (millis() - lastDataMs < 15000)) {
          size_t toRead = min(remaining, sizeof(buf));
          int len = stream->readBytes(buf, toRead);
          if (len > 0) {
            ttsFile.write(buf, len);
            totalBytes += len;
            remaining  -= len;
            lastDataMs  = millis();
          } else { vTaskDelay(1); }
        }
        while (stream->available() < 2 && millis() - lastDataMs < 1000) vTaskDelay(1);
        if (stream->available() >= 2) { stream->read(); stream->read(); }
        vTaskDelay(1);
      }
      if (diag) diag->t8_ttsStreamDone = millis();

      ttsFile.close();
      if (diag) { diag->t9_sdWriteDone = millis(); diag->pcmBytesBuffered = totalBytes; }

      if (totalBytes == 0) {
        SD_MMC.remove("/tts/" + ttsFilename);
        ttsFilename = "";
      } else {
        Serial.printf("TTS WAV saved: %s (%d bytes at 24kHz)\n", ttsFilename.c_str(), totalBytes);
        if (diag) {
          File check = SD_MMC.open("/tts/" + ttsFilename, FILE_READ);
          if (check) { diag->ttsWavBytes = check.size(); check.close(); }
        }
        refreshTTSList();
      }
    } else {
      ttsFilename = "";
    }
  } else {
    Serial.printf("[TTS] HTTP error %d\n", httpCode);
  }
  https.end();
  ttsInProgress = false;
  return ttsFilename;
}

// ============================================================================
// CAMERA SETUP
// ============================================================================
void setupCamera() {
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sscb_sda  = SIOD_GPIO_NUM;
  config.pin_sscb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.frame_size    = FRAMESIZE_SVGA;
  config.jpeg_quality  = 12;
  config.fb_count      = 1;
  config.grab_mode     = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);
  s->set_hmirror(s, 1);
}

// ============================================================================
// WEB INTERFACE HTML
// ============================================================================
// (Unchanged from v1.0.0 / v3.2.0 - full web UI retained)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>ESP32-S3 OpenAI + TTS</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f5f5f5; padding: 20px; }
.page-wrap { max-width: 1400px; margin: 0 auto; }
.header { background: linear-gradient(135deg, #0B2545, #1a3a6b); color: white; padding: 14px 20px; border-radius: 10px; margin-bottom: 20px; display: flex; justify-content: space-between; align-items: center; }
.header h2 { font-size: 18px; }
.header a { color: white; background: rgba(255,255,255,0.25); padding: 6px 14px; border-radius: 8px; text-decoration: none; font-size: 13px; font-weight: 600; border: 1px solid rgba(255,255,255,0.4); }
.main-grid { display: grid; grid-template-columns: 1.5fr 1fr; grid-template-rows: auto auto auto auto auto; gap: 20px; }
.video-section { grid-column: 1; grid-row: 1 / 5; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.audio-section { grid-column: 2; grid-row: 1; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.return-section { grid-column: 2; grid-row: 2; display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
.files-section  { grid-column: 2; grid-row: 3; display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 15px; }
.message-section { grid-column: 1 / 3; grid-row: 4; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.video-controls { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }
.video-display { background: #000; border-radius: 8px; position: relative; padding-bottom: 75%; overflow: hidden; }
.video-display img { position: absolute; width: 100%; height: 100%; object-fit: contain; }
.audio-bar { background: #f0f0f0; border-radius: 8px; padding: 15px; margin-bottom: 15px; text-align: center; font-weight: bold; color: #333; }
.recording-timer { font-size: 32px; color: #e74c3c; font-family: monospace; margin: 15px 0; text-align: center; }
.processing-msg  { text-align: center; color: #667eea; font-size: 16px; margin: 15px 0; font-weight: bold; }
.file-column, .return-column { background: white; border-radius: 12px; border: 2px solid #ddd; padding: 15px; }
.file-column h3, .return-column h3 { margin-bottom: 10px; color: #333; font-size: 14px; border-bottom: 2px solid #0B2545; padding-bottom: 8px; }
.file-list, .return-list { max-height: 150px; overflow-y: auto; margin-bottom: 10px; }
.file-item { background: #f8f8f8; padding: 8px; margin-bottom: 5px; border-radius: 6px; cursor: pointer; border: 2px solid transparent; font-size: 12px; }
.file-item:hover { background: #e8e8e8; }
.file-item.selected { border-color: #0B2545; background: #e8eeff; }
.message-display { background: #f9f9f9; border-radius: 8px; padding: 15px; min-height: 150px; max-height: 300px; overflow-y: auto; margin-bottom: 15px; font-size: 14px; line-height: 1.6; border: 1px solid #ddd; white-space: pre-wrap; }
.message-controls { display: flex; gap: 10px; }
button { padding: 10px 20px; font-size: 14px; font-weight: 600; border: none; border-radius: 8px; cursor: pointer; transition: all 0.2s; color: white; }
button:hover:not(:disabled) { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.15); }
button:disabled { opacity: 0.5; cursor: not-allowed; }
.btn-stream  { background: #3498db; }
.btn-stream.active { background: #27ae60; animation: pulse 1.5s infinite; }
.btn-capture { background: #0B2545; }
.btn-review  { background: #9b59b6; }
.btn-record  { background: #e74c3c; }
.btn-playback{ background: #2ecc71; }
.btn-delete  { background: #e67e22; }
.btn-save    { background: #16a085; }
.btn-send    { background: #0B2545; flex: 1; }
.btn-replay  { background: #34495e; flex: 1; }
.btn-record.recording { animation: pulse 1.5s infinite; }
.streaming-indicator { position: absolute; top: 10px; left: 10px; background: rgba(39,174,96,0.9); color: white; padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: bold; display: none; z-index: 10; }
.streaming-indicator.active { display: flex; align-items: center; gap: 8px; }
.streaming-indicator::before { content: ''; width: 8px; height: 8px; background: white; border-radius: 50%; animation: blink 1s infinite; }
@keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.6; } }
.status-bar { position: fixed; top: 20px; right: 20px; background: #2ecc71; color: white; padding: 12px 20px; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.2); display: none; z-index: 1000; max-width: 300px; }
.status-bar.show { display: block; }
.status-bar.error { background: #e74c3c; }
.info-banner { background: #fff3cd; padding: 12px; border-radius: 8px; margin-bottom: 15px; border-left: 4px solid #C9A227; font-size: 13px; }
.remote-status { background: #e8f4fd; border: 2px solid #0B2545; border-radius: 8px; padding: 10px 14px; margin-bottom: 15px; font-size: 12px; color: #0B2545; }
.remote-panel { background: white; border-radius: 12px; border: 2px solid #0B2545; padding: 15px; margin-bottom: 20px; }
.remote-panel h3 { color: #0B2545; font-size: 14px; margin-bottom: 12px; border-bottom: 2px solid #C9A227; padding-bottom: 8px; }
.remote-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }
.rem-btn { padding: 8px 6px; font-size: 11px; font-weight: 600; border: 2px solid #ccc; border-radius: 8px; background: #f5f5f5; color: #444; cursor: default; text-align: center; transition: all 0.15s; user-select: none; }
.rem-btn.remote-active { background: #fff3cd; border-color: #C9A227; color: #7a5800; box-shadow: 0 0 0 3px rgba(201,162,39,0.35); animation: remPulse 0.6s infinite alternate; }
.rem-btn.remote-done-ok  { background: #d4edda; border-color: #27ae60; color: #155724; animation: none; }
.rem-btn.remote-done-err { background: #f8d7da; border-color: #e74c3c; color: #721c24; animation: none; }
@keyframes remPulse { from { box-shadow: 0 0 0 2px rgba(201,162,39,0.3); } to { box-shadow: 0 0 0 6px rgba(201,162,39,0.05); } }
/* Serial Monitor panel */
.serial-panel { background: #0d1117; border-radius: 12px; border: 2px solid #30363d; padding: 15px; margin-bottom: 20px; }
.serial-panel-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px; }
.serial-panel-header h3 { color: #e6edf3; font-size: 14px; margin: 0; }
.serial-indicator { display: flex; align-items: center; gap: 8px; font-size: 12px; color: #8b949e; }
.serial-dot { width: 10px; height: 10px; border-radius: 50%; background: #30363d; transition: background 0.3s; }
.serial-dot.active { background: #2ea043; box-shadow: 0 0 6px #2ea043; animation: serialBlink 1s infinite; }
.serial-dot.idle   { background: #d29922; }
.serial-dot.off    { background: #6e7681; }
@keyframes serialBlink { 0%,100%{opacity:1} 50%{opacity:0.4} }
.serial-stats { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; margin-bottom: 10px; }
.serial-stat { background: #161b22; border: 1px solid #30363d; border-radius: 6px; padding: 6px 8px; text-align: center; }
.serial-stat .s-val { font-size: 16px; font-weight: 700; color: #58a6ff; font-family: monospace; }
.serial-stat .s-lbl { font-size: 10px; color: #8b949e; margin-top: 2px; }
.serial-log { background: #010409; border: 1px solid #21262d; border-radius: 6px; padding: 10px; height: 200px; overflow-y: auto; font-family: 'Courier New', monospace; font-size: 11px; line-height: 1.5; }
.serial-log .log-rx  { color: #58a6ff; }
.serial-log .log-tx  { color: #2ea043; }
.serial-log .log-hb  { color: #6e7681; }
.serial-log .log-err { color: #f85149; }
.serial-log .log-sys { color: #d29922; }
.serial-diag-btn { background: #21262d; color: #e6edf3; border: 1px solid #30363d; border-radius: 6px; padding: 6px 12px; font-size: 12px; cursor: pointer; transition: background 0.2s; }
.serial-diag-btn:hover { background: #30363d; }
</style>
</head>
<body>
<div id="statusBar" class="status-bar"></div>
<div class="page-wrap">
<div class="header">
  <h2>ESP32-S3 AI Camera &mdash; Automation Agent v1.2.41</h2>
  <div style="display:flex;gap:8px;align-items:center">
    <a href="/settings">&#9881; Settings</a>
    <button onclick="openDiagPanel()" style="background:rgba(201,162,39,0.85);color:white;border:1px solid rgba(255,255,255,0.4);padding:6px 14px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer">&#128202; Diagnostics</button>
  </div>
</div>
<div id="remoteStatus" class="remote-status">Remote: Waiting for UNO connection... | Image: none | Audio: none</div>

<div class="remote-panel">
  <h3>&#128247; Remote Control Status (UNO pins)</h3>
  <div class="remote-grid">
    <div class="rem-btn" id="remBtnTakePic">Take Pic</div>
    <div class="rem-btn" id="remBtnDeletePic">Del Pic</div>
    <div class="rem-btn" id="remBtnRecordAudio">Record</div>
    <div class="rem-btn" id="remBtnDeleteAudio">Del Audio</div>
    <div class="rem-btn" id="remBtnNextImage">Next Img</div>
    <div class="rem-btn" id="remBtnPrevImage">Prev Img</div>
    <div class="rem-btn" id="remBtnNextAudio">Next Audio</div>
    <div class="rem-btn" id="remBtnPrevAudio">Prev Audio</div>
    <div class="rem-btn" id="remBtnSelectImage">Sel Image</div>
    <div class="rem-btn" id="remBtnSelectAudio">Sel Audio</div>
    <div class="rem-btn" id="remBtnExport">Export</div>
    <div class="rem-btn" id="remBtnReplayTTS">Replay TTS</div>
  </div>
</div>
<div class="serial-panel">
  <div class="serial-panel-header">
    <h3>&#128268; Serial Monitor &mdash; UNO Link</h3>
    <div style="display:flex;gap:8px;align-items:center">
      <button class="serial-diag-btn" onclick="runDiag()">&#128202; Run Diagnostic</button>
      <button class="serial-diag-btn" onclick="clearLog()">&#128465; Clear</button>
      <div class="serial-indicator">
        <div class="serial-dot off" id="serialDot"></div>
        <span id="serialStatusText">Waiting...</span>
      </div>
    </div>
  </div>
  <div class="serial-stats">
    <div class="serial-stat"><div class="s-val" id="sRx">0</div><div class="s-lbl">Bytes RX</div></div>
    <div class="serial-stat"><div class="s-val" id="sTx">0</div><div class="s-lbl">Bytes TX</div></div>
    <div class="serial-stat"><div class="s-val" id="sCmds">0</div><div class="s-lbl">Commands</div></div>
    <div class="serial-stat"><div class="s-val" id="sErrs" style="color:#f85149">0</div><div class="s-lbl">Errors</div></div>
  </div>
  <div class="serial-log" id="serialLog"><span class="log-sys">-- Serial Monitor Ready --</span></div>
</div>

<div class="main-grid">
  <div class="video-section">
    <div class="info-banner"><strong>Pairing Workflow:</strong> Capture image/audio &rarr; Save to export &rarr; Send to OpenAI agent</div>
    <div class="video-controls">
      <button class="btn-stream" onclick="toggleStream()">Video Stream</button>
      <button class="btn-capture" onclick="captureImage()">Capture</button>
      <button class="btn-review" onclick="reviewLastImage()">Review</button>
    </div>
    <div class="video-display">
      <div id="streamingIndicator" class="streaming-indicator">LIVE</div>
      <img id="videoStream" src="">
    </div>
  </div>
  <div class="audio-section">
    <div class="audio-bar">Audio Playback Bar</div>
    <audio id="audioPlayer" controls style="width:100%; margin-bottom:15px"></audio>
    <div id="recordingTimer" class="recording-timer" style="display:none">00:00</div>
    <div id="processingMsg" class="processing-msg" style="display:none">Recording audio, please wait...</div>
    <div class="video-controls">
      <button id="recordBtn" class="btn-record" onclick="toggleRecording()">Record</button>
      <button id="playbackBtn" class="btn-playback" onclick="playbackAudio()" disabled>Speaker</button>
    </div>
  </div>
  <div class="return-section">
    <div class="return-column">
      <h3>Image for Export</h3>
      <div class="file-item" id="exportedImageDisplay" style="min-height:60px;display:flex;align-items:center;justify-content:center;color:#999;font-size:13px">None selected</div>
    </div>
    <div class="return-column">
      <h3>Audio for Export</h3>
      <div class="file-item" id="exportedAudioDisplay" style="min-height:60px;display:flex;align-items:center;justify-content:center;color:#999;font-size:13px">None selected</div>
    </div>
  </div>
  <div class="files-section">
    <div class="file-column">
      <h3>Image Files (Resources)</h3>
      <div class="file-list" id="imageFileList"></div>
      <div class="video-controls" style="margin-top:10px">
        <button class="btn-save" onclick="addSelectedImageToExport()" style="flex:1" id="imageToExportBtn" disabled>Add to Export</button>
        <button class="btn-save" onclick="downloadSelectedImage()" style="flex:1;background:#27ae60" id="imageDownloadBtn" disabled>Save to PC</button>
      </div>
      <div class="video-controls" style="margin-top:5px">
        <button class="btn-delete" onclick="deleteSelectedImageFile()" style="flex:1" id="imageFileDeleteBtn" disabled>Delete File</button>
      </div>
    </div>
    <div class="file-column">
      <h3>Audio Files (Resources)</h3>
      <div class="file-list" id="audioFileList"></div>
      <div class="video-controls" style="margin-top:10px">
        <button class="btn-save" onclick="addSelectedAudioToExport()" style="flex:1" id="audioToExportBtn" disabled>Add to Export</button>
        <button class="btn-save" onclick="downloadSelectedAudio()" style="flex:1;background:#27ae60" id="audioDownloadBtn" disabled>Save to PC</button>
      </div>
      <div class="video-controls" style="margin-top:5px">
        <button class="btn-delete" onclick="deleteSelectedAudioFile()" style="flex:1" id="audioFileDeleteBtn" disabled>Delete File</button>
      </div>
    </div>
    <div class="file-column">
      <h3>TTS Files (Resources)</h3>
      <div class="file-list" id="ttsFileList"></div>
      <div class="video-controls" style="margin-top:10px">
        <button class="btn-playback" onclick="playTTSSpeaker()" style="flex:1" id="ttsSpkBtn" disabled>Speaker</button>
        <button class="btn-replay" onclick="playTTSWeb()" style="flex:1" id="ttsWebBtn" disabled>Web</button>
      </div>
      <div class="video-controls" style="margin-top:5px">
        <button class="btn-delete" onclick="deleteSelectedTTSFile()" style="flex:1" id="ttsFileDeleteBtn" disabled>Delete File</button>
      </div>
    </div>
  </div>
  <div class="message-section">
    <h3 style="margin-bottom:15px;color:#333">OpenAI Analysis Results</h3>
    <div class="message-display" id="messageDisplay">OpenAI analysis with TTS will appear here...</div>
    <div class="message-controls">
      <button class="btn-send" onclick="sendToOpenAI()">Send to OpenAI</button>
      <button class="btn-replay" onclick="replayTTSSpeaker()" id="replaySpkBtn" disabled>Replay: Speaker</button>
      <button class="btn-replay" style="background:#2c3e50" onclick="replayTTSWeb()" id="replayWebBtn" disabled>Replay: Web</button>
      <button class="btn-save" onclick="downloadAnalysisPackage()">Export Package</button>
    </div>
  </div>
</div>
<script>
let recordingStartTime=0,timerInterval=null,streamActive=false,streamInterval=null;
let selectedImageFile="",selectedAudioFile="",selectedTTSFile="",selectedImageForExport="",selectedAudioForExport="";
let lastAnalysisResponse="",lastTTSAudioFile="",lastAnalysisID="",isRecording=false;

function showStatus(msg, isError=false) {
  const s=document.getElementById('statusBar');
  s.textContent=msg; s.className='status-bar show'+(isError?' error':'');
  setTimeout(()=>s.classList.remove('show'),3000);
}
function toggleStream() {
  const vs=document.getElementById('videoStream'),ind=document.getElementById('streamingIndicator'),btn=document.querySelector('.btn-stream');
  streamActive=!streamActive;
  if(streamActive){vs.src='/stream?'+Date.now();startStreamRefresh();ind.classList.add('active');btn.classList.add('active');btn.textContent='Stop Stream';showStatus('Stream started');}
  else{stopStreamRefresh();vs.src='';ind.classList.remove('active');btn.classList.remove('active');btn.textContent='Video Stream';showStatus('Stream stopped');}
}
function startStreamRefresh(){if(streamInterval)clearInterval(streamInterval);streamInterval=setInterval(()=>{if(streamActive&&!isRecording)document.getElementById('videoStream').src='/stream?'+Date.now();},150);}
function stopStreamRefresh(){if(streamInterval){clearInterval(streamInterval);streamInterval=null;}}
function captureImage(){fetch('/capture').then(r=>r.json()).then(d=>{if(d.success){showStatus('Captured: '+d.filename);loadImageList();}else showStatus('Capture failed',true);}).catch(()=>showStatus('Capture error',true));}
function reviewLastImage(){fetch('/image/latest').then(r=>r.json()).then(d=>{if(d.success&&d.filename){stopStreamRefresh();streamActive=false;document.getElementById('videoStream').src='/image?file='+encodeURIComponent(d.filename);showStatus('Reviewing: '+d.filename);}else showStatus('No images',true);}).catch(()=>showStatus('Review failed',true));}
function loadImageList(){fetch('/list/images').then(r=>r.json()).then(d=>{const l=document.getElementById('imageFileList');l.innerHTML='';if(!d.files.length){l.innerHTML='<div style="text-align:center;color:#999;padding:10px">No images</div>';return;}d.files.forEach(f=>{const i=document.createElement('div');i.className='file-item';if(f===selectedImageFile)i.classList.add('selected');i.textContent=f;i.onclick=()=>selectImageFile(f);l.appendChild(i);});});}
function loadAudioList(){fetch('/list/audio').then(r=>r.json()).then(d=>{const l=document.getElementById('audioFileList');l.innerHTML='';if(!d.files.length){l.innerHTML='<div style="text-align:center;color:#999;padding:10px">No audio</div>';return;}d.files.forEach(f=>{const i=document.createElement('div');i.className='file-item';if(f===selectedAudioFile)i.classList.add('selected');i.textContent=f;i.onclick=()=>selectAudioFile(f);l.appendChild(i);});});}
function loadTTSList(){fetch('/list/tts').then(r=>r.json()).then(d=>{const l=document.getElementById('ttsFileList');l.innerHTML='';if(!d.files.length){l.innerHTML='<div style="text-align:center;color:#999;padding:10px">No TTS files</div>';return;}d.files.forEach(f=>{const i=document.createElement('div');i.className='file-item';if(f===selectedTTSFile)i.classList.add('selected');i.textContent=f;i.onclick=()=>selectTTSFile(f);l.appendChild(i);});});}
function selectImageFile(fn){selectedImageFile=fn;document.querySelectorAll('#imageFileList .file-item').forEach(i=>i.classList.toggle('selected',i.textContent===fn));document.getElementById('imageToExportBtn').disabled=false;document.getElementById('imageDownloadBtn').disabled=false;document.getElementById('imageFileDeleteBtn').disabled=false;stopStreamRefresh();streamActive=false;document.getElementById('streamingIndicator').classList.remove('active');document.querySelector('.btn-stream').classList.remove('active');document.querySelector('.btn-stream').textContent='Video Stream';document.getElementById('videoStream').src='/image?file='+encodeURIComponent(fn);showStatus('Viewing: '+fn);}
function selectAudioFile(fn){selectedAudioFile=fn;document.querySelectorAll('#audioFileList .file-item').forEach(i=>i.classList.toggle('selected',i.textContent===fn));document.getElementById('audioToExportBtn').disabled=false;document.getElementById('audioDownloadBtn').disabled=false;document.getElementById('audioFileDeleteBtn').disabled=false;const ap=document.getElementById('audioPlayer');ap.src='/audio?file='+encodeURIComponent(fn);ap.style.display='block';document.getElementById('playbackBtn').disabled=false;showStatus('Selected: '+fn);}
function selectTTSFile(fn){selectedTTSFile=fn;document.querySelectorAll('#ttsFileList .file-item').forEach(i=>i.classList.toggle('selected',i.textContent===fn));document.getElementById('ttsSpkBtn').disabled=false;document.getElementById('ttsWebBtn').disabled=false;document.getElementById('ttsFileDeleteBtn').disabled=false;showStatus('Selected TTS: '+fn);}
function playTTSSpeaker(){if(!selectedTTSFile){showStatus('No TTS file selected',true);return;}showStatus('Playing on speaker...');fetch('/tts/speaker?file='+encodeURIComponent(selectedTTSFile),{signal:AbortSignal.timeout(120000)}).then(r=>r.json()).then(d=>{if(d.success)showStatus('Speaker playback complete');else showStatus('Speaker playback failed',true);}).catch(()=>showStatus('Speaker request failed',true));}
function playTTSWeb(){if(!selectedTTSFile){showStatus('No TTS file selected',true);return;}const ap=document.getElementById('audioPlayer');ap.src='/tts?file='+encodeURIComponent(selectedTTSFile);ap.style.display='block';ap.currentTime=0;ap.play();showStatus('Playing in browser');}
function deleteSelectedTTSFile(){if(!selectedTTSFile){showStatus('No TTS file selected',true);return;}if(confirm('Delete '+selectedTTSFile+'?')){fetch('/delete/tts?file='+encodeURIComponent(selectedTTSFile),{method:'DELETE'}).then(()=>{showStatus('Deleted: '+selectedTTSFile);selectedTTSFile="";document.getElementById('ttsSpkBtn').disabled=true;document.getElementById('ttsWebBtn').disabled=true;document.getElementById('ttsFileDeleteBtn').disabled=true;loadTTSList();});}}
function addSelectedImageToExport(){if(!selectedImageFile){showStatus('No image selected',true);return;}selectedImageForExport=selectedImageFile;document.getElementById('exportedImageDisplay').textContent=selectedImageFile;document.getElementById('exportedImageDisplay').style.color='#333';showStatus('Export: '+selectedImageFile);}
function addSelectedAudioToExport(){if(!selectedAudioFile){showStatus('No audio selected',true);return;}selectedAudioForExport=selectedAudioFile;document.getElementById('exportedAudioDisplay').textContent=selectedAudioFile;document.getElementById('exportedAudioDisplay').style.color='#333';showStatus('Export: '+selectedAudioFile);}
function downloadSelectedImage(){if(!selectedImageFile){showStatus('No image selected',true);return;}window.location.href='/image?file='+encodeURIComponent(selectedImageFile);}
function downloadSelectedAudio(){if(!selectedAudioFile){showStatus('No audio selected',true);return;}window.location.href='/audio?file='+encodeURIComponent(selectedAudioFile);}
function deleteSelectedImageFile(){if(!selectedImageFile){showStatus('No image selected',true);return;}if(confirm('Delete '+selectedImageFile+'?')){fetch('/delete/image?file='+encodeURIComponent(selectedImageFile),{method:'DELETE'}).then(()=>{showStatus('Deleted: '+selectedImageFile);if(selectedImageForExport===selectedImageFile){selectedImageForExport="";document.getElementById('exportedImageDisplay').textContent='None selected';document.getElementById('exportedImageDisplay').style.color='#999';}selectedImageFile="";document.getElementById('imageToExportBtn').disabled=true;document.getElementById('imageDownloadBtn').disabled=true;document.getElementById('imageFileDeleteBtn').disabled=true;loadImageList();});}}
function deleteSelectedAudioFile(){if(!selectedAudioFile){showStatus('No audio selected',true);return;}if(confirm('Delete '+selectedAudioFile+'?')){fetch('/delete/audio?file='+encodeURIComponent(selectedAudioFile),{method:'DELETE'}).then(()=>{showStatus('Deleted: '+selectedAudioFile);if(selectedAudioForExport===selectedAudioFile){selectedAudioForExport="";document.getElementById('exportedAudioDisplay').textContent='None selected';document.getElementById('exportedAudioDisplay').style.color='#999';}selectedAudioFile="";document.getElementById('audioPlayer').src='';document.getElementById('playbackBtn').disabled=true;document.getElementById('audioToExportBtn').disabled=true;document.getElementById('audioDownloadBtn').disabled=true;document.getElementById('audioFileDeleteBtn').disabled=true;loadAudioList();});}}
function updateTimer(){const e=Math.floor((Date.now()-recordingStartTime)/1000);document.getElementById('recordingTimer').textContent=String(Math.floor(e/60)).padStart(2,'0')+':'+String(e%60).padStart(2,'0');}
function toggleRecording(){const btn=document.getElementById('recordBtn');if(isRecording){clearInterval(timerInterval);const dur=Math.floor((Date.now()-recordingStartTime)/1000);stopStreamRefresh();btn.textContent='Processing...';btn.disabled=true;btn.classList.remove('recording');document.getElementById('recordingTimer').style.display='none';document.getElementById('processingMsg').style.display='block';showStatus('Recording '+dur+' seconds...');const to=(dur+10)*1000;Promise.race([fetch('/audio/record?duration='+dur,{signal:AbortSignal.timeout(to)}),new Promise((_,r)=>setTimeout(()=>r(new Error('timeout')),to))]).then(r=>r.json()).then(d=>{if(d.success){showStatus('Audio saved: '+d.filename);loadAudioList();}else showStatus('Recording failed',true);}).catch(()=>{showStatus('Recording timeout',true);setTimeout(loadAudioList,2000);}).finally(()=>{btn.textContent='Record';btn.disabled=false;document.getElementById('processingMsg').style.display='none';isRecording=false;if(streamActive)startStreamRefresh();});}else{isRecording=true;recordingStartTime=Date.now();btn.classList.add('recording');btn.textContent='Stop';document.getElementById('recordingTimer').style.display='block';timerInterval=setInterval(updateTimer,100);showStatus('Recording started');}}
function playbackAudio(){if(!selectedAudioFile){showStatus('No audio selected',true);return;}showStatus('Playing on speaker...');fetch('/audio/speaker?file='+encodeURIComponent(selectedAudioFile),{signal:AbortSignal.timeout(60000)}).then(r=>r.json()).then(d=>{if(d.success)showStatus('Playback complete');else showStatus('Playback failed',true);}).catch(()=>showStatus('Playback timeout',true));}
function sendToOpenAI(){if(!selectedImageForExport||!selectedAudioForExport){showStatus('Select image and audio for export first',true);return;}lastAnalysisID=Date.now().toString();stopStreamRefresh();showStatus('Starting OpenAI analysis...');document.getElementById('messageDisplay').textContent='Initializing analysis...';fetch('/openai/analyze',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({image:selectedImageForExport,audio:selectedAudioForExport,id:lastAnalysisID})}).then(r=>r.json()).then(d=>{if(d.status==='processing'){const chk=()=>{fetch('/openai/progress').then(r=>r.json()).then(p=>{if(p.resultReady){clearInterval(pi);if(p.success){lastAnalysisResponse=p.response;lastTTSAudioFile=p.ttsFile;document.getElementById('messageDisplay').textContent=p.response;showStatus('Analysis complete! Loading audio...');enableReplayButtons();loadTTSList();if(p.ttsFile&&p.ttsFile!=='none'&&p.ttsFile!==''){const audioPlayer=document.getElementById('audioPlayer');audioPlayer.src='/tts?file='+encodeURIComponent(p.ttsFile);audioPlayer.style.display='block';audioPlayer.currentTime=0;audioPlayer.play().then(()=>{showStatus('Analysis complete! Playing in browser...');}).catch(()=>{showStatus('Analysis complete! Click play to hear response.');});}}else{document.getElementById('messageDisplay').textContent='Analysis failed:\n\n'+p.response;showStatus('Analysis failed',true);}if(streamActive)startStreamRefresh();}else if(p.inProgress){const filled=Math.floor(p.percent/5);const bar='='.repeat(filled)+'>'+'-'.repeat(Math.max(0,19-filled));document.getElementById('messageDisplay').textContent=p.stage+' - '+p.percent+'%\n['+bar+']\n\n'+p.detail+'\n\nThis process takes 60-90 seconds total.\nPlease wait...';showStatus(p.stage+' - '+p.percent+'%');}}).catch(()=>{clearInterval(pi);showStatus('Progress check failed',true);if(streamActive)startStreamRefresh();});};chk();const pi=setInterval(chk,1500);}else{showStatus('Failed to start analysis',true);document.getElementById('messageDisplay').textContent='Error: '+(d.error||'Unknown error');if(streamActive)startStreamRefresh();}}).catch(()=>{showStatus('Request failed',true);document.getElementById('messageDisplay').textContent='Request failed. Check serial monitor for details.';if(streamActive)startStreamRefresh();});}
function replayTTSSpeaker(){if(!lastTTSAudioFile||lastTTSAudioFile==='none'){showStatus('No TTS audio available',true);return;}showStatus('Playing on speaker...');fetch('/tts/speaker?file='+encodeURIComponent(lastTTSAudioFile),{signal:AbortSignal.timeout(120000)}).then(r=>r.json()).then(d=>{if(d.success)showStatus('Speaker playback complete');else showStatus('Speaker playback failed',true);}).catch(()=>showStatus('Speaker request failed',true));}
function replayTTSWeb(){if(!lastTTSAudioFile||lastTTSAudioFile==='none'){showStatus('No TTS audio available',true);return;}const ap=document.getElementById('audioPlayer');ap.src='/tts?file='+encodeURIComponent(lastTTSAudioFile);ap.style.display='block';ap.currentTime=0;ap.play();showStatus('Playing in browser');}
function enableReplayButtons(){document.getElementById('replaySpkBtn').disabled=false;document.getElementById('replayWebBtn').disabled=false;}
function downloadAnalysisPackage(){if(!lastAnalysisID){showStatus('Complete an analysis first',true);return;}showStatus('Preparing package...');window.location.href='/analysis/export?id='+lastAnalysisID;setTimeout(()=>showStatus('Package downloaded'),1000);}
// -- Serial Monitor panel -----------------------------------------------------
let serialLogCache = [];   // Last log lines we've already rendered
let serialAutoScroll = true;

function classForLine(line) {
  if (line.startsWith('>>'))   return 'log-rx';
  if (line.startsWith('<<'))   return 'log-tx';
  if (line.startsWith('[HB]')) return 'log-hb';
  if (line.includes('ERROR'))  return 'log-err';
  return 'log-sys';
}

function renderSerialLog(lines) {
  const box = document.getElementById('serialLog');
  // Only re-render if content changed
  const key = lines.join('|');
  if (key === serialLogCache.join('|')) return;
  serialLogCache = lines;
  box.innerHTML = lines.map(l =>
    `<div class="${classForLine(l)}">${escHtml(l)}</div>`
  ).join('');
  if (serialAutoScroll) box.scrollTop = box.scrollHeight;
}

function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function clearLog() {
  serialLogCache = [];
  document.getElementById('serialLog').innerHTML =
    '<span class="log-sys">-- Log cleared --</span>';
}

function runDiag() {
  // Sends CMD:DIAG via a small fetch -- the ESP32 prints the report to
  // its own Serial Monitor and replies with OK:DIAG|...
  fetch('/serial/cmd', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({cmd: 'CMD:DIAG'})
  }).then(r => r.json())
    .then(d => showStatus('Diagnostic sent -- check Serial Monitor'))
    .catch(() => showStatus('Diagnostic request failed', true));
}

// Poll /serial/diag every 1.5 seconds
setInterval(() => {
  fetch('/serial/diag')
    .then(r => r.json())
    .then(d => {
      // Stats
      document.getElementById('sRx').textContent   = fmtNum(d.totalRx);
      document.getElementById('sTx').textContent   = fmtNum(d.totalTx);
      document.getElementById('sCmds').textContent = d.cmdCount;
      document.getElementById('sErrs').textContent = d.errCount;

      // Activity indicator
      const dot  = document.getElementById('serialDot');
      const txt  = document.getElementById('serialStatusText');
      const idle = d.lastActivityMs > 5000;
      if (d.active) {
        dot.className = 'serial-dot active';
        txt.textContent = 'Active -- last cmd ' + (d.lastActivityMs/1000).toFixed(1) + 's ago';
      } else if (d.totalRx > 0) {
        dot.className = 'serial-dot idle';
        txt.textContent = 'Idle -- ' + (d.lastActivityMs/1000|0) + 's since last byte';
      } else {
        dot.className = 'serial-dot off';
        txt.textContent = 'No UNO activity yet';
      }

      // Log
      if (d.log && d.log.length > 0) renderSerialLog(d.log);
    })
    .catch(() => {});
}, 1500);

function fmtNum(n) {
  if (n >= 1000) return (n/1000).toFixed(1) + 'k';
  return String(n);
}

// -- Remote button state feedback ---------------------------------------------
// Maps activeCmd values from ESP32 to the matching button element IDs
const CMD_BUTTON_MAP = {
  'TAKE_PIC':     'remBtnTakePic',
  'DELETE_PIC':   'remBtnDeletePic',
  'RECORD_AUDIO': 'remBtnRecordAudio',
  'DELETE_AUDIO': 'remBtnDeleteAudio',
  'NEXT_IMAGE':   'remBtnNextImage',
  'PREV_IMAGE':   'remBtnPrevImage',
  'NEXT_AUDIO':   'remBtnNextAudio',
  'PREV_AUDIO':   'remBtnPrevAudio',
  'SELECT_IMAGE': 'remBtnSelectImage',
  'SELECT_AUDIO': 'remBtnSelectAudio',
  'EXPORT':       'remBtnExport',
  'REPLAY_TTS':   'remBtnReplayTTS',
};

// v1.2.39: Server now latches activeCmd and cmdDone for REMOTE_LATCH_MS (2s)
// so the browser is guaranteed to see them. JS just mirrors what the server says.
let lastHighlightedBtn = '';

function applyButtonState(activeCmd, cmdDone, cmdResult) {
  // Clear any previously highlighted button if a different command is now active
  if (lastHighlightedBtn && lastHighlightedBtn !== activeCmd) {
    const prev = document.getElementById(CMD_BUTTON_MAP[lastHighlightedBtn]);
    if (prev) prev.classList.remove('remote-active', 'remote-done-ok', 'remote-done-err');
    lastHighlightedBtn = '';
  }

  if (activeCmd && CMD_BUTTON_MAP[activeCmd]) {
    const el = document.getElementById(CMD_BUTTON_MAP[activeCmd]);
    if (el) {
      if (cmdDone) {
        // Server is in the done-latch window: show result colour
        el.classList.remove('remote-active');
        el.classList.add(cmdResult === 'OK' ? 'remote-done-ok' : 'remote-done-err');
      } else {
        // Command still in progress: pulse yellow
        el.classList.remove('remote-done-ok', 'remote-done-err');
        el.classList.add('remote-active');
      }
      lastHighlightedBtn = activeCmd;
    }
  } else if (!activeCmd && lastHighlightedBtn) {
    // Latch expired on server side -- clean up any residual highlight
    const el = document.getElementById(CMD_BUTTON_MAP[lastHighlightedBtn]);
    if (el) el.classList.remove('remote-active', 'remote-done-ok', 'remote-done-err');
    lastHighlightedBtn = '';
  }
}

// Poll remote status every 800ms for snappy feedback
setInterval(() => {
  fetch('/remote/status')
    .then(r => r.json())
    .then(d => {
      // Status bar text
      document.getElementById('remoteStatus').textContent =
        'Remote: ' + d.status +
        ' | Image: ' + (d.selectedImage || 'none') +
        ' | Audio: ' + (d.selectedAudio || 'none');

      // Button state feedback
      applyButtonState(d.activeCmd, d.cmdDone, d.cmdResult);

      // Sync export queue display if set remotely
      if (d.selectedImage) {
        document.getElementById('exportedImageDisplay').textContent = d.selectedImage;
        document.getElementById('exportedImageDisplay').style.color = '#333';
        selectedImageForExport = d.selectedImage;
      }
      if (d.selectedAudio) {
        document.getElementById('exportedAudioDisplay').textContent = d.selectedAudio;
        document.getElementById('exportedAudioDisplay').style.color = '#333';
        selectedAudioForExport = d.selectedAudio;
      }

      if (d.reloadLists) { loadImageList(); loadAudioList(); loadTTSList(); }

      if (d.ttsReady && d.ttsFile) {
        lastTTSAudioFile = d.ttsFile;
        enableReplayButtons();
      }
    });
}, 800);
loadImageList(); loadAudioList(); loadTTSList();
setTimeout(()=>toggleStream(), 500);

// -- Diagnostics panel -----------------------------------------------------
function openDiagPanel() {
  document.getElementById('diagOverlay').style.display='flex';
  fetchDiagData();
}
function closeDiagPanel() {
  document.getElementById('diagOverlay').style.display='none';
}
function diagColor(ms) {
  if (ms === 0) return '#999';
  if (ms < 1000) return '#27ae60';
  if (ms < 5000) return '#e67e22';
  return '#e74c3c';
}
function diagBar(ms, max) {
  const pct = Math.min(100, Math.round(ms / max * 100));
  const col = diagColor(ms);
  return `<div style="display:inline-block;width:${pct}%;min-width:2px;height:10px;background:${col};border-radius:3px;vertical-align:middle"></div>`;
}
// Returns {color, label, symbol} for an API key length value.
// Valid OpenAI keys are ~51-80 chars. <20 = placeholder. >100 = corrupted/doubled.
function keyHealthFromLen(n) {
  if (n >= 40 && n <= 220) return { color:'#27ae60', symbol:'&#10003;', label:'valid length'      };
  if (n > 220)             return { color:'#e74c3c', symbol:'&#9888;',  label:'SUSPICIOUS (' + n + ' chars &mdash; too long) RE-ENTER KEY' };
  if (n >= 10)             return { color:'#e67e22', symbol:'&#9888;',  label:'short &mdash; check key' };
                           return { color:'#e74c3c', symbol:'&#10007;', label:'NOT SET'             };
}
function fetchDiagData() {
  const el = document.getElementById('diagContent');
  el.innerHTML = '<p style="color:#888;text-align:center;padding:20px">Fetching diagnostics...</p>';
  fetch('/diagnostics').then(r=>r.json()).then(d=>{
    if (!d.available) {
      el.innerHTML = '<p style="color:#888;text-align:center;padding:20px">'+d.message+'</p>';
      return;
    }
    const ms = d.deltas;
    const maxMs = Math.max(ms.ms_total, 1);
    const badge = d.mode==='synthetic'
      ? '<span style="background:#c9a227;color:white;padding:2px 8px;border-radius:10px;font-size:11px;margin-left:8px">SYNTHETIC</span>'
      : '<span style="background:#27ae60;color:white;padding:2px 8px;border-radius:10px;font-size:11px;margin-left:8px">LIVE</span>';
    const status = d.completed
      ? '<span style="color:#27ae60">&#10003; Complete</span>'
      : `<span style="color:#e74c3c">&#10007; Failed @ ${d.errorStage}</span>`;

    const stages = [
      { label:'Base64 Encoding',       ms: ms.ms_b64,       note:'SD read + mbedtls encode' },
      { label:'GPT-4 Vision (network)',ms: ms.ms_vision,    note:'HTTP POST to response received' },
      { label:'TTS Time-to-First-Byte',ms: ms.ms_ttsFB,     note:'POST sent -> first audio chunk' },
      { label:'TTS Stream Download',   ms: ms.ms_ttsStream, note:'First byte -> last byte' },
      { label:'PSRAM->SD Write',        ms: ms.ms_sdWrite,   note:'WAV header + PCM flush to SD' },
      { label:'UI Hold (browser delay)',ms:ms.ms_uiHold,    note:'pendingPlay set -> playback start' },
      { label:'Audio Playback',        ms: ms.ms_play,      note:'playWAVSpeaker() duration' },
    ];

    let stageHTML = stages.map(s => {
      const c = diagColor(s.ms);
      return `<tr>
        <td style="padding:6px 8px;color:#555;font-size:13px">${s.label}</td>
        <td style="padding:6px 8px;text-align:right;font-weight:bold;color:${c};font-size:13px;white-space:nowrap">${s.ms.toLocaleString()} ms</td>
        <td style="padding:6px 8px;width:120px">${diagBar(s.ms, maxMs)}</td>
        <td style="padding:6px 8px;color:#999;font-size:11px">${s.note}</td>
      </tr>`;
    }).join('');

    const mx = d.metrics;
    el.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px">
        <div><strong>Run:</strong> ${d.runID} ${badge}</div>
        <div><strong>Status:</strong> ${status}</div>
        <div style="font-size:13px;color:#555"><strong>Total:</strong>
          <span style="color:${diagColor(ms.ms_total)};font-weight:bold">${(ms.ms_total/1000).toFixed(1)}s</span>
        </div>
      </div>
      <table style="width:100%;border-collapse:collapse;margin-bottom:16px">
        <thead><tr style="background:#f0f0f0">
          <th style="padding:6px 8px;text-align:left;font-size:12px;color:#666">Stage</th>
          <th style="padding:6px 8px;text-align:right;font-size:12px;color:#666">Duration</th>
          <th style="padding:6px 8px;font-size:12px;color:#666">Bar</th>
          <th style="padding:6px 8px;text-align:left;font-size:12px;color:#666">Notes</th>
        </tr></thead>
        <tbody>${stageHTML}</tbody>
      </table>
      <details style="margin-bottom:12px">
        <summary style="cursor:pointer;font-weight:bold;color:#0B2545;padding:6px 0">Memory &amp; File Metrics</summary>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px;font-size:13px">
          <div><span style="color:#666">Image file:</span> <strong>${mx.imgFileBytes.toLocaleString()} bytes</strong></div>
          <div><span style="color:#666">Base64 output:</span> <strong>${mx.b64Chars.toLocaleString()} chars</strong></div>
          <div><span style="color:#666">Audio input:</span> <strong>${mx.audioFileBytes.toLocaleString()} bytes</strong></div>
          <div><span style="color:#666">PCM buffered (PSRAM):</span> <strong>${mx.pcmBytesBuffered.toLocaleString()} bytes</strong></div>
          <div><span style="color:#666">TTS WAV on SD:</span> <strong>${mx.ttsWavBytes.toLocaleString()} bytes</strong></div>
          <div><span style="color:#666">WiFi RSSI:</span> <strong>${mx.wifiRSSI} dBm</strong></div>
          <div><span style="color:#666">API key length:</span> <strong id="diagKeyHealth"></strong></div>
          <div><span style="color:#666">Heap free (start):</span> <strong>${(mx.heapFreeStart/1024).toFixed(0)} KB</strong></div>
          <div><span style="color:#666">PSRAM free (start):</span> <strong>${(mx.psramFreeStart/1024).toFixed(0)} KB</strong></div>
          <div><span style="color:#666">PSRAM after buf:</span> <strong>${(mx.psramFreeAfter/1024).toFixed(0)} KB</strong></div>
        </div>
      </details>
      <div style="display:flex;gap:8px;font-size:12px;color:#888;margin-top:4px">
        <span style="color:#27ae60">&#9632; &lt;1s</span>
        <span style="color:#e67e22">&#9632; 1-5s</span>
        <span style="color:#e74c3c">&#9632; &gt;5s</span>
        <span style="color:#999">&#9632; not measured</span>
      </div>`;
    // Populate key health AFTER innerHTML is set (element now exists in DOM)
    const kh = keyHealthFromLen(mx.apiKeyLen);
    const khEl = document.getElementById('diagKeyHealth');
    if (khEl) {
      khEl.style.color = kh.color;
      khEl.innerHTML = mx.apiKeyLen + ' chars ' + kh.symbol + ' ' + kh.label;
    }
  }).catch(()=>{
    el.innerHTML = '<p style="color:#e74c3c;padding:20px">Failed to fetch diagnostics</p>';
  });
}
function runSyntheticTest() {
  if (!confirm('Run synthetic pipeline test? This will call GPT-4 Vision and TTS APIs (counts against your quota).')) return;
  showStatus('Starting synthetic pipeline test...');
  fetch('/test/pipeline', {method:'POST'}).then(r=>r.json()).then(d=>{
    showStatus(d.message || 'Test started');
    // Auto-refresh diagnostics panel after 90s when test should be done
    setTimeout(()=>{ if(document.getElementById('diagOverlay').style.display!=='none') fetchDiagData(); }, 90000);
  }).catch(()=>showStatus('Test request failed',true));
  closeDiagPanel();
}
function clearDiagData() {
  fetch('/diag/clear',{method:'DELETE'}).then(()=>{ fetchDiagData(); showStatus('Diagnostics cleared'); });
}
</script>
<!-- Diagnostics overlay panel -->
<div id="diagOverlay" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,0.55);z-index:9999;align-items:center;justify-content:center">
  <div style="background:white;border-radius:14px;width:min(820px,95vw);max-height:90vh;overflow-y:auto;box-shadow:0 8px 40px rgba(0,0,0,0.3)">
    <div style="background:linear-gradient(135deg,#0B2545,#1a3a6b);color:white;padding:16px 20px;border-radius:14px 14px 0 0;display:flex;justify-content:space-between;align-items:center">
      <span style="font-size:17px;font-weight:700">&#128202; Pipeline Timing Diagnostics</span>
      <button onclick="closeDiagPanel()" style="background:none;border:none;color:white;font-size:22px;cursor:pointer;line-height:1">&times;</button>
    </div>
    <div style="padding:20px">
      <div id="diagContent" style="min-height:100px"></div>
      <div style="display:flex;gap:10px;margin-top:16px;flex-wrap:wrap">
        <button onclick="fetchDiagData()" style="background:#0B2545;color:white;border:none;padding:8px 16px;border-radius:8px;cursor:pointer;font-size:13px">&#8635; Refresh</button>
        <button onclick="runSyntheticTest()" style="background:#c9a227;color:white;border:none;padding:8px 16px;border-radius:8px;cursor:pointer;font-size:13px">&#9654; Run Synthetic Test</button>
        <button onclick="clearDiagData()" style="background:#e74c3c;color:white;border:none;padding:8px 16px;border-radius:8px;cursor:pointer;font-size:13px">&#128465; Clear</button>
        <button onclick="closeDiagPanel()" style="background:#888;color:white;border:none;padding:8px 16px;border-radius:8px;cursor:pointer;font-size:13px">Close</button>
      </div>
    </div>
  </div>
</div>
</div>
)rawliteral";

// ============================================================================
// WEB HANDLER FUNCTIONS
// ============================================================================
void handleStream() {
  if (recordingInProgress || ttsInProgress) { server.send(503); return; }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(503); return; }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "application/json", "{\"success\":false}"); return; }
  imageCount++;
  String filename = "IMG_" + String(imageCount) + ".jpg";
  File file = SD_MMC.open("/images/" + filename, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    lastCapturedImage = filename;
    refreshImageList();
    currentImageIndex = imageFileList.size() - 1;
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
  esp_camera_fb_return(fb);
}

void handleImageView() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/images/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "image/jpeg"); file.close(); }
  else server.send(404);
}

void handleLatestImage() {
  File root = SD_MMC.open("/images");
  String latest = "";
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) latest = String(file.name());
  }
  if (latest.length() > 0) server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + latest + "\"}");
  else server.send(200, "application/json", "{\"success\":false}");
}

void handleAudioRecord() {
  if (recordingInProgress || ttsInProgress) {
    server.send(503, "application/json", "{\"success\":false,\"error\":\"busy\"}");
    return;
  }
  recordingInProgress = true;
  ledOn();

  int duration = server.arg("duration").toInt();
  if (duration <= 0) duration = 5;

  int totalSamples = SAMPLE_RATE * duration;
  int16_t* audioBuffer = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) audioBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) {
    recordingInProgress = false; ledOff();
    server.send(500, "application/json", "{\"success\":false,\"error\":\"memory\"}");
    return;
  }

  size_t samplesRead = 0;
  while (samplesRead < totalSamples) {
    size_t bytesRead = i2s_mic.readBytes((char*)(audioBuffer + samplesRead),
                                         (totalSamples - samplesRead) * sizeof(int16_t));
    samplesRead += bytesRead / sizeof(int16_t);
    if (samplesRead % 16000 == 0) yield();
  }

  for (int i = 0; i < totalSamples; i++) {
    int32_t amplified = (int32_t)audioBuffer[i] * AMPLIFICATION;
    audioBuffer[i] = constrain(amplified, -32768, 32767);
  }

  audioCount++;
  String filename = "REC_" + String(audioCount) + ".wav";
  File file = SD_MMC.open("/audio/" + filename, FILE_WRITE);

  if (file) {
    uint32_t fileSize = 44 + (totalSamples * 2);
    file.write((uint8_t*)"RIFF", 4);
    uint32_t chunkSize = fileSize - 8; file.write((uint8_t*)&chunkSize, 4);
    file.write((uint8_t*)"WAVE", 4);
    file.write((uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16; file.write((uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1; file.write((uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1; file.write((uint8_t*)&numChannels, 2);
    uint32_t sampleRate = SAMPLE_RATE; file.write((uint8_t*)&sampleRate, 4);
    uint32_t byteRate = SAMPLE_RATE * 2; file.write((uint8_t*)&byteRate, 4);
    uint16_t blockAlign = 2; file.write((uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16; file.write((uint8_t*)&bitsPerSample, 2);
    file.write((uint8_t*)"data", 4);
    uint32_t dataSize = totalSamples * 2; file.write((uint8_t*)&dataSize, 4);
    file.write((uint8_t*)audioBuffer, totalSamples * 2);
    file.close();

    free(audioBuffer);
    recordingInProgress = false;
    ledOff();
    lastRecordedAudio = filename;
    refreshAudioList();
    currentAudioIndex = audioFileList.size() - 1;
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    free(audioBuffer);
    recordingInProgress = false;
    ledOff();
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleAudioStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "audio/wav"); file.close(); }
  else server.send(404);
}

void handleSpeakerPlayback() {
  String filename = server.arg("file");
  server.send(200, "application/json", "{\"success\":true}");
  delay(30);
  playWAVSpeaker("/audio/" + filename);
}

void handleTTSStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/tts/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "audio/wav"); file.close(); }
  else server.send(404);
}

// handleTTSSpeaker() -- plays a TTS WAV file through the MAX98357A speaker.
// TTS files are saved at 24kHz (OpenAI native rate). i2s_spk MUST be reinitialized
// at TTS_SAMPLE_RATE (24kHz) before playback -- feeding 24kHz PCM through a 16kHz
// I2S clock produces 67% speed playback, low pitch, and DMA buffer noise.
// After playback i2s_spk is restored to SAMPLE_RATE (16kHz) for recorded audio
// compatibility. This function is intentionally separate from handleSpeakerPlayback()
// which handles REC files at 16kHz -- do not merge or route TTS through playWAVSpeaker().
// Route: GET /tts/speaker?file=TTS_xxx.wav  (v1.2.37: was calling playWAVSpeaker at 16kHz)
void handleTTSSpeaker() {
  String filename = server.arg("file");
  if (filename.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"no file\"}");
    return;
  }

  // Stop PDM mic -- eliminates ~1MHz PDM clock coupling into I2S speaker lines.
  i2s_mic.end();

  File file = SD_MMC.open("/tts/" + filename, FILE_READ);
  if (!file) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"file not found\"}");
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }

  size_t fileSize = file.size();
  if (fileSize <= 44) {
    file.close();
    server.send(200, "application/json", "{\"success\":true}");
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }
  file.seek(44);
  size_t pcmSize = fileSize - 44;

  // Reinitialize speaker at TTS_SAMPLE_RATE (24kHz).
  // The saved WAV contains 24kHz PCM -- playing it through a 16kHz-clocked I2S
  // peripheral produces 67% speed playback with noise and DMA halting.
  i2s_spk.end();
  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, TTS_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[TTS Speaker] ERROR: i2s_spk reinit at 24kHz failed");
    file.close();
    server.send(500, "application/json", "{\"success\":false,\"error\":\"i2s init failed\"}");
    i2s_spk.end();
    i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
    i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }
  Serial.printf("[TTS Speaker] Playing %s at 24kHz (%u bytes PCM)\n", filename.c_str(), pcmSize);

  // DC-blocking IIR high-pass filter -- removes DC bias, prevents speaker thud.
  static const int32_t HP_ALPHA_Q15 = 32764;  // 0.9999 x 32768
  int32_t hp_x_prev = 0;
  int32_t hp_y_prev = 0;

  // PSRAM fast-path: load entire PCM before I2S begins -- eliminates SD latency
  // stalls that cause DMA starvation and audible dropouts during playback.
  size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint8_t* pcmBuf  = nullptr;
  bool usingPSRAM  = false;

  if (psramFree > pcmSize + 65536) {
    pcmBuf = (uint8_t*)heap_caps_malloc(pcmSize, MALLOC_CAP_SPIRAM);
    if (pcmBuf) {
      size_t bytesRead = file.read(pcmBuf, pcmSize);
      file.close();
      if (bytesRead == pcmSize) {
        usingPSRAM = true;
        Serial.printf("[TTS Speaker] PSRAM fast-path: %u bytes\n", pcmSize);
      } else {
        heap_caps_free(pcmBuf);
        pcmBuf = nullptr;
        file = SD_MMC.open("/tts/" + filename, FILE_READ);
        if (file) file.seek(44);
      }
    }
  }

  static uint8_t monoBuffer[4096];
  static uint8_t stereoBuffer[8192];

  if (usingPSRAM) {
    size_t offset    = 0;
    uint8_t yieldCnt = 0;
    while (offset < pcmSize) {
      size_t chunk = min((size_t)sizeof(monoBuffer), pcmSize - offset);
      memcpy(monoBuffer, pcmBuf + offset, chunk);
      offset += chunk;
      if (chunk & 1) chunk--;
      int16_t* ms = (int16_t*)monoBuffer;
      int16_t* ss = (int16_t*)stereoBuffer;
      size_t n = chunk / 2;
      for (size_t i = 0; i < n; i++) {
        int32_t x = (int32_t)ms[i];
        int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
        hp_x_prev = x;
        hp_y_prev = y;
        // Attenuate to prevent MAX98357A clipping (TTS_PLAY_ATTENUATION = 1 -> -3dB)
        int16_t s = (int16_t)constrain(y >> TTS_PLAY_ATTENUATION, -32768, 32767);
        // Fade-out on last chunk for clean mute boundary
        if (offset >= pcmSize) {
          s = (int16_t)((int32_t)s * (int32_t)(n - i) / (int32_t)n);
        }
        ss[i * 2]     = s;
        ss[i * 2 + 1] = s;
      }
      i2s_spk.write(stereoBuffer, n * 4);
      if (++yieldCnt >= 32) { yieldCnt = 0; taskYIELD(); }
    }
    heap_caps_free(pcmBuf);

  } else {
    // SD fallback -- stream directly when PSRAM is insufficient
    Serial.printf("[TTS Speaker] SD fallback: %u bytes\n", pcmSize);
    uint8_t chunkCnt = 0;
    while (file && file.available()) {
      size_t bytesRead = file.read(monoBuffer, sizeof(monoBuffer));
      if (bytesRead > 1) {
        if (bytesRead & 1) bytesRead--;
        int16_t* ms = (int16_t*)monoBuffer;
        int16_t* ss = (int16_t*)stereoBuffer;
        size_t n = bytesRead / 2;
        for (size_t i = 0; i < n; i++) {
          int32_t x = (int32_t)ms[i];
          int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
          hp_x_prev = x;
          hp_y_prev = y;
          int16_t s = (int16_t)constrain(y >> TTS_PLAY_ATTENUATION, -32768, 32767);
          if (!file.available()) {
            s = (int16_t)((int32_t)s * (int32_t)(n - i) / (int32_t)n);
          }
          ss[i * 2]     = s;
          ss[i * 2 + 1] = s;
        }
        i2s_spk.write(stereoBuffer, n * 4);
      }
      if (++chunkCnt >= 32) { chunkCnt = 0; taskYIELD(); }
    }
    if (file) file.close();
  }

  // Post-flush: hold DMA at silence so MAX98357A mute circuit engages cleanly (~50ms)
  i2sFlushZeros((TTS_SAMPLE_RATE / 20) * 4);

  // Restore speaker to 16kHz for recorded audio playback compatibility.
  i2s_spk.end();
  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[TTS Speaker] WARNING: i2s_spk restore to 16kHz failed");
  }

  // Restart PDM mic after playback.
  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[TTS Speaker] WARNING: PDM mic restart failed after TTS playback");
  }
  delay(20);  // PDM settle time

  server.send(200, "application/json", "{\"success\":true}");
}

void handleListImages() {
  File root = SD_MMC.open("/images");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      json += "\"" + String(file.name()) + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleListAudio() {
  File root = SD_MMC.open("/audio");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      json += "\"" + String(file.name()) + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDeleteImage() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/images/" + filename)) {
    refreshImageList();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleDeleteAudio() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/audio/" + filename)) {
    refreshAudioList();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleListTTS() {
  File root = SD_MMC.open("/tts");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      json += "\"" + String(file.name()) + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDeleteTTS() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/tts/" + filename)) {
    refreshTTSList();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

// Remote status endpoint (polled by web UI every 800ms)
bool   remoteReloadLists    = false;
bool   remoteTTSReady       = false;
String remoteTTSFile        = "";

// v1.2.39 button state feedback -- timestamp-latched so fast commands
// remain visible across at least 2 poll cycles (~1600ms).
//
// Root cause of original bug: PIN handlers run synchronously and complete
// before loop() ever calls server.handleClient(). By the time the browser
// polls /remote/status, remoteActiveCommand is already "" and
// remoteCommandDone has been cleared on the same poll it was read.
// The browser therefore never sees the active state and misses the done flash.
//
// Fix: remoteCommandBegin() latches the command name AND a start timestamp.
// handleRemoteStatus() reports the active command for as long as it is set OR
// for REMOTE_LATCH_MS after it cleared (so fast commands still show active).
// remoteCommandEnd() records a done timestamp instead of a bare bool.
// handleRemoteStatus() reports cmdDone=true for REMOTE_LATCH_MS after end,
// guaranteeing the browser sees it on at least 2 polls.

#define REMOTE_LATCH_MS  2000   // ms to hold active/done state visible

String remoteActiveCommand   = "";   // command name while executing
String remoteLastCommand     = "";   // last command, held for latch window
unsigned long remoteActiveMs = 0;    // millis() when Begin was called
unsigned long remoteDoneMs   = 0;    // millis() when End was called
String remoteCommandResult   = "";   // "OK" or "ERROR"

// Call at the START of every PIN handler
void remoteCommandBegin(String cmdName) {
  remoteActiveCommand = cmdName;
  remoteLastCommand   = cmdName;
  remoteActiveMs      = millis();
  remoteDoneMs        = 0;
  remoteCommandResult = "";
}

// Call at the END of every PIN handler
void remoteCommandEnd(bool success) {
  remoteActiveCommand = "";
  remoteDoneMs        = millis();
  remoteCommandResult = success ? "OK" : "ERROR";
}

void handleRemoteStatus() {
  unsigned long now = millis();

  // Determine what to report for activeCmd:
  // - If command is still executing, report it directly.
  // - If command just finished within the latch window, keep reporting it
  //   so the browser sees the active state before it sees done.
  String reportedActive = remoteActiveCommand;
  if (reportedActive.length() == 0 && remoteLastCommand.length() > 0) {
    if (remoteDoneMs > 0 && (now - remoteDoneMs) < REMOTE_LATCH_MS) {
      reportedActive = remoteLastCommand;  // hold visible during done flash
    } else if (remoteDoneMs == 0 && remoteActiveMs > 0 &&
               (now - remoteActiveMs) < REMOTE_LATCH_MS) {
      reportedActive = remoteLastCommand;  // fast command: latch active briefly
    }
  }

  // cmdDone is true for REMOTE_LATCH_MS after End was called
  bool reportedDone = (remoteDoneMs > 0 && (now - remoteDoneMs) < REMOTE_LATCH_MS);

  // Clear latch once the done window expires
  if (remoteDoneMs > 0 && (now - remoteDoneMs) >= REMOTE_LATCH_MS) {
    remoteLastCommand = "";
    remoteActiveMs    = 0;
    remoteDoneMs      = 0;
  }

  DynamicJsonDocument doc(512);
  doc["status"]        = analysisInProgress ? "Analyzing" : (recordingInProgress ? "Recording" : "Ready");
  doc["selectedImage"] = imageForExport;
  doc["selectedAudio"] = audioForExport;
  doc["reloadLists"]   = remoteReloadLists;
  doc["ttsReady"]      = remoteTTSReady;
  doc["ttsFile"]       = remoteTTSFile;
  doc["activeCmd"]     = reportedActive;
  doc["cmdDone"]       = reportedDone;
  doc["cmdResult"]     = remoteCommandResult;
  remoteReloadLists    = false;
  remoteTTSReady       = false;
  remoteTTSFile        = "";
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============================================================================
// OPENAI ANALYSIS PIPELINE
// ============================================================================
void runOpenAIAnalysis() {
  analysisResultReady   = false;
  analysisResultSuccess = false;

  // -- Diagnostics: initialise timing struct for this run --------------------
  PipelineTiming diag;
  diag.runID       = currentAnalysisID;
  diag.isSynthetic = false;
  diag.t0_taskStart   = millis();
  diag.heapFreeStart  = (uint32_t)ESP.getFreeHeap();
  diag.psramFreeStart = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  diag.wifiRSSI       = (int8_t)WiFi.RSSI();

  Serial.println("\n========================================");
  Serial.println("OpenAI Analysis Request");
  Serial.println("Image: " + currentImagePath);
  Serial.println("Audio: " + currentAudioPath);

  analysisProgressStage   = "Validation";
  analysisProgressDetail  = "Checking files...";
  analysisProgressPercent = 5;
  server.handleClient(); yield();

  if (!SD_MMC.exists(currentImagePath) || !SD_MMC.exists(currentAudioPath)) {
    diag.errorStage = "Validation";
    lastDiag = diag; diagAvailable = true;
    analysisInProgress    = false;
    analysisProgressStage = "Error";
    analysisResultText    = "Error: File not found";
    analysisResultSuccess = false;
    analysisResultReady   = true;
    speakText("Analysis error. One or more files not found.");
    return;
  }

  // -- Pre-flight: API key health --------------------------------------------
  String apiKeyCheck = sanitizeAPIKey(SettingsStore::getOpenAIKey());
  diag.apiKeyLen = (uint8_t)min((int)apiKeyCheck.length(), 255);
  if (apiKeyCheck.length() < 10 || !apiKeyCheck.startsWith("sk-")) {
    diag.errorStage = "API Key Invalid";
    lastDiag = diag; diagAvailable = true;
    analysisInProgress    = false;
    analysisProgressStage = "Error";
    analysisResultText    = "Error: OpenAI API key not configured. Visit /settings.";
    analysisResultSuccess = false;
    analysisResultReady   = true;
    speakText("Analysis error. API key not configured.");
    return;
  }

  // Capture audio file size for diagnostics
  { File af = SD_MMC.open(currentAudioPath, FILE_READ);
    if (af) { diag.audioFileBytes = af.size(); af.close(); } }

  analysisProgressStage   = "Whisper";
  analysisProgressDetail  = "Transcribing audio (30-40 sec)...";
  analysisProgressPercent = 10;
  server.handleClient(); yield();

  String transcription = transcribeAudioWithWhisper(currentAudioPath);
  if (transcription.startsWith("Error")) {
    diag.errorStage = "Whisper";
    lastDiag = diag; diagAvailable = true;
    analysisInProgress    = false;
    analysisProgressStage = "Error";
    analysisResultText    = transcription;
    analysisResultSuccess = false;
    analysisResultReady   = true;
    speakText("Audio transcription failed.");
    return;
  }

  analysisProgressStage   = "Vision";
  analysisProgressDetail  = "Analyzing image with GPT-4 (30-40 sec)...";
  analysisProgressPercent = 45;
  server.handleClient(); yield();

  String analysis = analyzeImageWithGPT4Vision(currentImagePath, transcription, &diag);
  if (analysis.startsWith("Error")) {
    diag.errorStage = "Vision";
    lastDiag = diag; diagAvailable = true;
    analysisInProgress    = false;
    analysisProgressStage = "Error";
    analysisResultText    = analysis;
    analysisResultSuccess = false;
    analysisResultReady   = true;
    speakText("Image analysis failed.");
    return;
  }

  analysisProgressStage   = "TTS";
  analysisProgressDetail  = "Generating speech audio (20-30 sec)...";
  analysisProgressPercent = 75;
  server.handleClient(); yield();

  String combinedText = "Audio Transcription: " + transcription + ". Image Analysis: " + analysis;
  String ttsFile = generateTTSAudio(combinedText, currentAnalysisID, &diag);
  if (ttsFile.length() == 0) ttsFile = "none";

  lastTTSFile = ttsFile;

  analysisProgressStage   = "Complete";
  analysisProgressDetail  = "Analysis finished successfully";
  analysisProgressPercent = 100;

  String result = "=== AUDIO TRANSCRIPTION ===\n" + transcription + "\n\n=== IMAGE ANALYSIS ===\n" + analysis;
  analysisResultText    = result;
  analysisResultTTSFile = ttsFile;
  analysisResultSuccess = true;
  analysisResultReady   = true;

  Serial.println("\n=== Analysis Complete ===");
  Serial.println("TTS File: " + ttsFile);

  saveAnalysisPackage(currentAnalysisID, transcription, analysis, currentImagePath, currentAudioPath, ttsFile);

  // Mark analysis done -- web browser can now retrieve result text via /openai/progress
  analysisInProgress = false;
  if (ttsFile != "none" && ttsFile.length() > 0) {
    remoteTTSFile = ttsFile;
  }

  // Send serial notification to UNO
  Serial.println("OK:ANALYSIS_COMPLETE|TTS:" + ttsFile);

  // v1.2.36: TTS plays in browser via audioPlayer -- no deferred speaker trigger
  diag.t10_flagSet = millis();

  // -- Store diag -------------------------------------------------------------
  diag.completed = true;
  lastDiag = diag; diagAvailable = true;
  printDiagTable(lastDiag);
}

void saveAnalysisPackage(String id, String transcription, String analysis, String imgPath, String audPath, String ttsFile) {
  Serial.println("\n=== Saving Analysis Package ===");
  String dir = "/analysis/" + id;
  SD_MMC.mkdir("/analysis");
  SD_MMC.mkdir(dir.c_str());

  File f;
  f = SD_MMC.open(dir + "/prompt.txt", FILE_WRITE);
  if (f) { f.println("Audio Transcription\n==================\n" + transcription); f.close(); }

  f = SD_MMC.open(dir + "/response.txt", FILE_WRITE);
  if (f) { f.println("Image Analysis\n==============\n" + analysis); f.close(); }

  f = SD_MMC.open(dir + "/combined.txt", FILE_WRITE);
  if (f) {
    f.println("OpenAI Analysis Results\n======================");
    f.println("Timestamp: " + id);
    f.println("Image: " + imgPath + "\nAudio: " + audPath);
    f.println("\n=== AUDIO TRANSCRIPTION ===\n" + transcription);
    f.println("\n=== IMAGE ANALYSIS ===\n" + analysis);
    f.close();
  }

  f = SD_MMC.open(dir + "/metadata.txt", FILE_WRITE);
  if (f) {
    f.println("Analysis Metadata\n=================");
    f.println("Analysis ID: " + id);
    f.println("Source Image: " + imgPath + "\nSource Audio: " + audPath);
    f.println("TTS File: /tts/" + ttsFile);
    f.close();
  }

  if (ttsFile != "none" && ttsFile.length() > 0) {
    File src = SD_MMC.open("/tts/" + ttsFile, FILE_READ);
    File dst = SD_MMC.open(dir + "/audio.wav", FILE_WRITE);
    if (src && dst) {
      uint8_t buf[512];
      while (src.available()) {
        size_t n = src.read(buf, sizeof(buf));
        if (n > 0) dst.write(buf, n);
      }
      src.close(); dst.close();
    }
  }
  Serial.println("Package saved: " + dir);
}

void handleOpenAIAnalyze() {
  if (analysisInProgress) {
    server.send(503, "application/json", "{\"success\":false,\"error\":\"Analysis already in progress\"}");
    return;
  }
  DynamicJsonDocument reqDoc(512);
  deserializeJson(reqDoc, server.arg("plain"));
  String imageFile = reqDoc["image"].as<String>();
  String audioFile = reqDoc["audio"].as<String>();
  currentAnalysisID   = reqDoc["id"].as<String>();
  currentImagePath    = "/images/" + imageFile;
  currentAudioPath    = "/audio/" + audioFile;
  analysisInProgress  = true;
  analysisResultReady = false;
  analysisProgressStage   = "Starting";
  analysisProgressDetail  = "Initializing analysis...";
  analysisProgressPercent = 0;
  server.send(202, "application/json", "{\"success\":true,\"status\":\"processing\"}");
  delay(100);
  runOpenAIAnalysis();
}

void handleOpenAIProgress() {
  DynamicJsonDocument doc(8192);
  doc["inProgress"]  = analysisInProgress;
  doc["stage"]       = analysisProgressStage;
  doc["detail"]      = analysisProgressDetail;
  doc["percent"]     = analysisProgressPercent;
  doc["resultReady"] = analysisResultReady;
  if (analysisResultReady) {
    doc["success"]  = analysisResultSuccess;
    doc["response"] = analysisResultText;
    doc["ttsFile"]  = analysisResultTTSFile;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAnalysisDownload() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, body);
  String id   = doc["id"].as<String>();
  String text = doc["text"].as<String>();
  File f = SD_MMC.open("/ANALYSIS_" + id + ".txt", FILE_WRITE);
  if (f) { f.println("OpenAI Analysis Results\n======================\n" + text); f.close(); }
  server.send(200, "application/json", "{\"success\":true,\"id\":\"" + id + "\"}");
}

void handleAnalysisPackage() {
  String id = server.arg("id");
  File file = SD_MMC.open("/ANALYSIS_" + id + ".txt", FILE_READ);
  if (file) { server.streamFile(file, "text/plain"); file.close(); }
  else server.send(404);
}

void handleAnalysisExport() {
  String id  = server.arg("id");
  String dir = "/analysis/" + id;
  File dirFile = SD_MMC.open(dir);
  if (!dirFile || !dirFile.isDirectory()) { server.send(404, "text/plain", "Analysis not found"); return; }
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Analysis " + id + "</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:800px;margin:50px auto;padding:20px;}";
  html += "h1{color:#0B2545;}.file-item{background:#ecf0f1;margin:10px 0;padding:15px;border-radius:5px;}";
  html += ".file-item a{color:#3498db;text-decoration:none;font-weight:bold;font-size:16px;}";
  html += ".btn{background:#0B2545;color:white;padding:12px 24px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin:20px 0;}";
  html += "</style></head><body><h1>Analysis Package</h1><p><strong>ID:</strong> " + id + "</p>";
  html += "<button class='btn' onclick='downloadAll()'>Download All Files</button><ul style='list-style:none;padding:0'>";
  String fileList = "";
  File f = dirFile.openNextFile();
  int count = 0;
  while (f) {
    if (!f.isDirectory()) {
      String fn = String(f.name());
      html += "<li class='file-item'><a href='/analysis/file?id=" + id + "&file=" + fn + "' download='" + fn + "'>" + fn + "</a>";
      html += "<span style='color:#999;font-size:13px;margin-left:10px'>(" + String(f.size()) + " bytes)</span></li>";
      fileList += "'" + fn + "',";
      count++;
    }
    f = dirFile.openNextFile();
  }
  dirFile.close();
  html += "</ul><p>Total files: " + String(count) + "</p>";
  html += "<script>function downloadAll(){const files=[" + fileList + "];files.forEach((fn,i)=>{setTimeout(()=>{const a=document.createElement('a');a.href='/analysis/file?id=" + id + "&file='+fn;a.download=fn;document.body.appendChild(a);a.click();document.body.removeChild(a);},i*500);});}";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void handleAnalysisFile() {
  String id       = server.arg("id");
  String filename = server.arg("file");
  String filepath = "/analysis/" + id + "/" + filename;
  File file = SD_MMC.open(filepath, FILE_READ);
  if (file) { server.streamFile(file, "application/octet-stream"); file.close(); }
  else server.send(404, "text/plain", "File not found");
}

// ============================================================================
// SETTINGS PAGE (from v3.2.0)
// ============================================================================
void handleSettingsPage() {
  String localIP = WiFi.localIP().toString();
  String html = R"rawliteral(<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>AI Camera - Settings</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#0B2545 0%,#1a3a6b 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
.container{background:white;border-radius:20px;padding:40px;max-width:520px;width:100%;box-shadow:0 20px 60px rgba(0,0,0,0.3);}
h1{color:#0B2545;margin-bottom:6px;font-size:26px;text-align:center;}
.subtitle{text-align:center;color:#888;margin-bottom:30px;font-size:13px;}
.back-link{display:inline-block;margin-bottom:20px;color:#0B2545;text-decoration:none;font-size:14px;font-weight:600;}
.section-title{font-size:12px;font-weight:700;color:#0B2545;text-transform:uppercase;letter-spacing:1px;margin:24px 0 16px;padding-bottom:6px;border-bottom:2px solid #e8e8f0;}
.form-group{margin-bottom:18px;}
label{display:block;margin-bottom:6px;color:#333;font-weight:600;font-size:14px;}
input{width:100%;padding:12px 14px;border:2px solid #e0e0e0;border-radius:10px;font-size:14px;}
input:focus{outline:none;border-color:#0B2545;}
.ip-badge{background:#f0f4ff;border:2px solid #0B2545;border-radius:10px;padding:14px 18px;margin-bottom:24px;text-align:center;}
.ip-badge .label{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:1px;}
.ip-badge .ip{font-size:22px;font-weight:700;color:#0B2545;font-family:monospace;margin-top:4px;}
.btn{width:100%;padding:14px;background:linear-gradient(135deg,#0B2545,#1a3a6b);color:white;border:none;border-radius:10px;font-size:16px;font-weight:700;cursor:pointer;margin-top:10px;}
.help{color:#999;font-size:12px;margin-top:4px;}
.toggle-pw{float:right;font-size:12px;color:#0B2545;cursor:pointer;margin-top:-22px;font-weight:600;}
</style></head><body><div class="container">
<a class="back-link" href="/">&larr; Back to Camera</a>
<h1>Camera Settings</h1>
<p class="subtitle">Automation Agent v1.2.41 - Update WiFi and API credentials</p>
<div class="ip-badge"><div class="label">Current IP Address</div><div class="ip">)rawliteral";
  html += localIP;
  html += R"rawliteral(</div></div>
<form method="POST" action="/settings/save">
<div class="section-title">WiFi Network</div>
<div class="form-group"><label>WiFi Network (SSID)</label>
<input type="text" name="ssid" value=")rawliteral";
  html += SettingsStore::getWiFiSSID();
  html += R"rawliteral(" required></div>
<div class="form-group"><label>WiFi Password</label>
<input type="password" name="password" id="wifiPw" placeholder="Leave blank to keep current">
<span class="toggle-pw" onclick="document.getElementById('wifiPw').type=document.getElementById('wifiPw').type==='password'?'text':'password'">Show</span>
<div class="help">Leave blank to keep existing password</div></div>
<div class="section-title">OpenAI API Key</div>
<div class="form-group"><label>API Key</label>
<input type="password" name="apikey" id="apiPw" maxlength="220" placeholder="sk-... (leave blank to keep current)" oninput="document.getElementById('apikeyCount').textContent=this.value.length+' / 220 chars'">
<span class="toggle-pw" onclick="document.getElementById('apiPw').type=document.getElementById('apiPw').type==='password'?'text':'password'">Show</span>
<div class="help" id="apikeyCount">0 / 220 chars</div>)rawliteral";

  // Show stored key health -- length and partial chars so user can verify
  // without the full key being visible in the page source.
  // NOTE: Modern OpenAI Project keys (sk-proj-*) are ~164 chars as of late 2024.
  // Old-format keys were ~56 chars. Both are valid. Only flag if >220 or <20.
  String storedKey = sanitizeAPIKey(SettingsStore::getOpenAIKey());
  int kLen = storedKey.length();
  String keyHealth = "";
  if (kLen < 20) {
    keyHealth = "<div style='background:#fff3cd;border:1px solid #ffc107;border-radius:8px;padding:10px;margin-top:6px;font-size:13px'>"
                "&#9888; Key appears to be a placeholder or not set (length: " + String(kLen) + " chars). "
                "Enter your real OpenAI API key above.</div>";
  } else if (kLen > 220) {
    keyHealth = "<div style='background:#f8d7da;border:1px solid #dc3545;border-radius:8px;padding:10px;margin-top:6px;font-size:13px'>"
                "&#10007; Key is unusually long (" + String(kLen) + " chars). "
                "Modern OpenAI keys are ~56-164 chars. This may cause errors. Please re-enter your key.</div>";
  } else {
    String prefix = storedKey.substring(0, 7);   // show "sk-proj" or "sk-..." prefix
    String suffix = storedKey.substring(kLen - 4);
    keyHealth = "<div style='background:#d4edda;border:1px solid #28a745;border-radius:8px;padding:10px;margin-top:6px;font-size:13px'>"
                "&#10003; Key stored: " + String(kLen) + " chars &mdash; <code>" + prefix + "......" + suffix + "</code> "
                "(leave blank to keep)</div>";
  }
  html += keyHealth;
  html += R"rawliteral(
<div class="section-title">Device</div>
<div class="form-group"><label>Device Name</label>
<input type="text" name="devicename" value=")rawliteral";
  html += SettingsStore::getDeviceName();
  html += R"rawliteral("></div>
<button type="submit" class="btn">Save &amp; Restart Camera</button>
</form></div></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleSettingsSave() {
  String ssid       = server.arg("ssid");
  String password   = server.arg("password");
  String apiKey     = server.arg("apikey");
  String deviceName = server.arg("devicename");
  if (ssid.length() == 0)        ssid       = SettingsStore::getWiFiSSID();
  if (password.length() == 0)    password   = SettingsStore::getWiFiPassword();
  if (apiKey.length() == 0)      apiKey     = SettingsStore::getOpenAIKey();
  if (deviceName.length() == 0)  deviceName = SettingsStore::getDeviceName();

  // Sanitize API key before writing to NVS -- prevents corrupt/doubled keys
  // from being persisted (the source of the 164-char key bug in v1.2.18).
  String cleanKey = sanitizeAPIKey(apiKey);
  Serial.printf("[Settings] Saving API key: %d chars sanitized from %d raw chars\n",
                cleanKey.length(), apiKey.length());
  if (cleanKey.length() != apiKey.length()) {
    Serial.println("[Settings] WARNING: Key was sanitized before save -- check for copy-paste artifacts");
  }

  SettingsStore::save(ssid, password, cleanKey, deviceName);
  Serial.println("\n=== Settings Updated ===");
  Serial.println("SSID: " + ssid);
  server.send(200, "text/html", "<html><body><h2>Settings saved. Restarting...</h2></body></html>");
  delay(2000);
  ESP.restart();
}

// ============================================================================
// SERIAL COMMAND INTERFACE
// ============================================================================
void sendSerialResponse(String response) {
  UNOSerial.println(response);   // to UNO via Gravity connector
  UNOSerial.flush();
  Serial.println(">> " + response);  // mirror to USB monitor for debugging
  serialTotalTx += response.length() + 2;
  if (response.startsWith("ERROR:")) serialErrCount++;
  serialLogPush("<< " + response);
}
void sendSerialOK(String data = "")      { if (data.length() > 0) sendSerialResponse("OK:" + data); else sendSerialResponse("OK"); }
void sendSerialError(String message)     { sendSerialResponse("ERROR:" + message); }

String extractCommand(String input) {
  int cmdStart = input.indexOf("CMD:");
  if (cmdStart == -1) {
    cmdStart = input.indexOf("PIN:");
    if (cmdStart != -1) return "PIN:" + input.substring(cmdStart + 4, input.indexOf("|", cmdStart) == -1 ? input.length() : input.indexOf("|", cmdStart));
    return "";
  }
  int cmdEnd = input.indexOf("|", cmdStart);
  if (cmdEnd == -1) cmdEnd = input.length();
  String cmd = input.substring(cmdStart + 4, cmdEnd);
  cmd.trim();
  return cmd;
}

// ============================================================================
// CORE SERIAL HANDLERS (original v1.0.0)
// ============================================================================
void handleSerialPing()    { sendSerialOK("PONG"); }
void handleSerialVersion() { sendSerialOK("AUTOMATION_AGENT_" + String(FIRMWARE_VERSION)); }

void handleSerialStatus() {
  String status = "READY";
  if (recordingInProgress) status = "RECORDING";
  if (analysisInProgress)  status = "ANALYZING";
  int iCount = 0, aCount = 0;
  File d = SD_MMC.open("/images"); if (d) { while(File f=d.openNextFile()) { if(!f.isDirectory()) iCount++; } d.close(); }
  d = SD_MMC.open("/audio");       if (d) { while(File f=d.openNextFile()) { if(!f.isDirectory()) aCount++; } d.close(); }
  sendSerialOK(status + "|HEAP:" + String(ESP.getFreeHeap()) + "|IMAGES:" + String(iCount) + "|AUDIO:" + String(aCount));
}

void handleSerialCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { sendSerialError("Camera capture failed"); return; }
  imageCount++;
  String filename = "IMG_" + String(imageCount) + ".jpg";
  File file = SD_MMC.open("/images/" + filename, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len); file.close();
    lastCapturedImage = filename;
    refreshImageList();
    currentImageIndex = imageFileList.size() - 1;
    esp_camera_fb_return(fb);
    sendSerialOK(filename);
  } else {
    esp_camera_fb_return(fb);
    sendSerialError("SD card write failed");
  }
}

void recordAudioToFile(int duration, String& filenameOut) {
  if (recordingInProgress || ttsInProgress) { filenameOut = ""; return; }
  recordingInProgress = true;
  ledOn();

  int totalSamples = SAMPLE_RATE * duration;
  int16_t* audioBuffer = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) audioBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) { recordingInProgress = false; ledOff(); filenameOut = ""; return; }

  size_t samplesRead = 0;
  while (samplesRead < totalSamples) {
    size_t bytesRead = i2s_mic.readBytes((char*)(audioBuffer + samplesRead),
                                          (totalSamples - samplesRead) * sizeof(int16_t));
    samplesRead += bytesRead / sizeof(int16_t);
    if (samplesRead % 16000 == 0) yield();
  }
  for (int i = 0; i < totalSamples; i++) {
    int32_t amp = (int32_t)audioBuffer[i] * AMPLIFICATION;
    audioBuffer[i] = constrain(amp, -32768, 32767);
  }
  audioCount++;
  String filename = "REC_" + String(audioCount) + ".wav";
  File file = SD_MMC.open("/audio/" + filename, FILE_WRITE);
  if (file) {
    uint32_t fileSize = 44 + totalSamples * 2;
    file.write((uint8_t*)"RIFF", 4); uint32_t cs = fileSize-8; file.write((uint8_t*)&cs, 4);
    file.write((uint8_t*)"WAVE", 4); file.write((uint8_t*)"fmt ", 4);
    uint32_t sc = 16; file.write((uint8_t*)&sc, 4);
    uint16_t af = 1; file.write((uint8_t*)&af, 2);
    uint16_t nc = 1; file.write((uint8_t*)&nc, 2);
    uint32_t sr = SAMPLE_RATE; file.write((uint8_t*)&sr, 4);
    uint32_t br = SAMPLE_RATE*2; file.write((uint8_t*)&br, 4);
    uint16_t ba = 2; file.write((uint8_t*)&ba, 2);
    uint16_t bp = 16; file.write((uint8_t*)&bp, 2);
    file.write((uint8_t*)"data", 4); uint32_t ds = totalSamples*2; file.write((uint8_t*)&ds, 4);
    file.write((uint8_t*)audioBuffer, totalSamples*2);
    file.close();
    free(audioBuffer);
    recordingInProgress = false; ledOff();
    lastRecordedAudio = filename;
    refreshAudioList();
    currentAudioIndex = audioFileList.size() - 1;
    filenameOut = filename;
  } else {
    free(audioBuffer); recordingInProgress = false; ledOff(); filenameOut = "";
  }
}

void handleSerialRecord(int duration) {
  if (duration <= 0 || duration > 60) { sendSerialError("Duration must be 1-60 seconds"); return; }
  String filename;
  recordAudioToFile(duration, filename);
  if (filename.length() > 0) sendSerialOK(filename);
  else sendSerialError("Recording failed");
}

void handleSerialListImages() {
  File root = SD_MMC.open("/images");
  if (!root) { sendSerialError("Cannot open images directory"); return; }
  String list = ""; int count = 0;
  File f = root.openNextFile();
  while (f) { if (!f.isDirectory()) { if (count>0) list+=","; list+=String(f.name()); count++; } f=root.openNextFile(); }
  root.close();
  sendSerialOK(count > 0 ? list : "NONE");
}

void handleSerialListAudio() {
  File root = SD_MMC.open("/audio");
  if (!root) { sendSerialError("Cannot open audio directory"); return; }
  String list = ""; int count = 0;
  File f = root.openNextFile();
  while (f) { if (!f.isDirectory()) { if (count>0) list+=","; list+=String(f.name()); count++; } f=root.openNextFile(); }
  root.close();
  sendSerialOK(count > 0 ? list : "NONE");
}

void handleSerialGetLastImage() {
  if (lastCapturedImage.length() > 0) sendSerialOK(lastCapturedImage);
  else sendSerialError("No image captured yet");
}

void handleSerialGetLastAudio() {
  if (lastRecordedAudio.length() > 0) sendSerialOK(lastRecordedAudio);
  else sendSerialError("No audio recorded yet");
}

// -- CMD:DIAG -- Full functional check over serial ------------------------------
// Runs a self-test sequence and prints a structured report to Serial Monitor.
// Safe to call from UNO at any time; does not modify any files.
void handleSerialDiag() {
  Serial.println("\n============================================");
  Serial.println("  SERIAL DIAGNOSTIC REPORT");
  Serial.printf( "  Firmware  : v%s\n", FIRMWARE_VERSION);
  Serial.printf( "  Uptime    : %lu s\n", millis() / 1000);
  Serial.println("============================================");

  // --- Serial port stats ---
  Serial.println("\n[SERIAL PORT]");
  Serial.printf("  Status    : %s\n",   serialPortActive ? "ACTIVE" : "IDLE");
  Serial.printf("  Bytes RX  : %lu\n",  serialTotalRx);
  Serial.printf("  Bytes TX  : %lu\n",  serialTotalTx);
  Serial.printf("  Commands  : %lu\n",  serialCmdCount);
  Serial.printf("  Errors    : %lu\n",  serialErrCount);
  Serial.printf("  Last RX   : %lu ms ago\n", millis() - serialLastActivityMs);

  // --- WiFi ---
  Serial.println("\n[WIFI]");
  Serial.printf("  Status    : %s\n",   WiFi.isConnected() ? "CONNECTED" : "DISCONNECTED");
  if (WiFi.isConnected()) {
    Serial.printf("  IP        : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI      : %d dBm\n", WiFi.RSSI());
  }

  // --- Memory ---
  Serial.println("\n[MEMORY]");
  Serial.printf("  Free Heap : %d bytes\n", ESP.getFreeHeap());
  Serial.printf("  PSRAM     : %d bytes\n", ESP.getFreePsram());

  // --- Storage ---
  Serial.println("\n[STORAGE]");
  Serial.printf("  Images    : %d files\n", (int)imageFileList.size());
  Serial.printf("  Audio     : %d files\n", (int)audioFileList.size());
  Serial.printf("  Last Img  : %s\n",       lastCapturedImage.length() > 0 ? lastCapturedImage.c_str() : "none");
  Serial.printf("  Last Audio: %s\n",       lastRecordedAudio.length() > 0 ? lastRecordedAudio.c_str() : "none");
  Serial.printf("  Export Img: %s\n",       imageForExport.length() > 0    ? imageForExport.c_str()    : "none");
  Serial.printf("  Export Aud: %s\n",       audioForExport.length() > 0    ? audioForExport.c_str()    : "none");

  // --- System state ---
  Serial.println("\n[SYSTEM STATE]");
  Serial.printf("  Recording : %s\n",       recordingInProgress ? "YES" : "no");
  Serial.printf("  Analyzing : %s\n",       analysisInProgress  ? "YES" : "no");
  Serial.printf("  TTS Active: %s\n",       ttsInProgress       ? "YES" : "no");
  Serial.printf("  API Key   : %s\n",       (SettingsStore::getOpenAIKey().startsWith("sk-YOUR") ||
                                              SettingsStore::getOpenAIKey().length() < 10)
                                              ? "NOT SET" : "Configured");

  // --- Functional checks ---
  Serial.println("\n[FUNCTIONAL CHECKS]");
  // Camera ping
  bool camOk = false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    camOk = true;
    Serial.printf("  Camera    : OK  (%d bytes, %dx%d)\n", fb->len, fb->width, fb->height);
    esp_camera_fb_return(fb);
  } else {
    Serial.println("  Camera    : FAIL - could not grab frame");
  }
  // SD card
  File testDir = SD_MMC.open("/images");
  if (testDir) {
    Serial.println("  SD Card   : OK");
    testDir.close();
  } else {
    Serial.println("  SD Card   : FAIL - cannot open /images");
  }
  // Serial loopback echo (UNO can verify it receives this)
  Serial.println("  Serial TX : OK  (loopback echo follows)");

  Serial.println("\n============================================");
  Serial.println("  END DIAGNOSTIC REPORT");
  Serial.println("============================================\n");

  // Structured OK response for UNO parser
  sendSerialOK("DIAG|CAM:" + String(camOk ? "OK" : "FAIL") +
               "|SD:OK" +
               "|HEAP:" + String(ESP.getFreeHeap()) +
               "|IMGS:" + String(imageFileList.size()) +
               "|AUD:"  + String(audioFileList.size()) +
               "|WIFI:" + String(WiFi.isConnected() ? "OK" : "DOWN"));
}

// -- /serial/diag -- JSON endpoint polled by web Serial Monitor panel -----------
void handleSerialDiagEndpoint() {
  DynamicJsonDocument doc(2048);
  doc["active"]        = serialPortActive;
  doc["totalRx"]       = serialTotalRx;
  doc["totalTx"]       = serialTotalTx;
  doc["cmdCount"]      = serialCmdCount;
  doc["errCount"]      = serialErrCount;
  doc["lastActivityMs"]= millis() - serialLastActivityMs;
  doc["uptime"]        = millis() / 1000;
  doc["freeHeap"]      = ESP.getFreeHeap();
  doc["wifiOk"]        = WiFi.isConnected();
  doc["rssi"]          = WiFi.RSSI();
  doc["activeCmd"]     = remoteActiveCommand;
  doc["recording"]     = recordingInProgress;
  doc["analyzing"]     = analysisInProgress;

  // Build ordered log array (oldest first)
  JsonArray log = doc.createNestedArray("log");
  int start = (serialLogCount < SERIAL_LOG_SIZE) ? 0 : serialLogHead;
  for (int i = 0; i < serialLogCount; i++) {
    int idx = (start + i) % SERIAL_LOG_SIZE;
    log.add(serialLog[idx]);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============================================================================
// v1.1.0 PIN COMMAND HANDLERS (triggered by UNO remote pins)
// Each handler: performs action + TTS announcement + LED feedback + serial OK
// ============================================================================

// PIN 4: Take Picture
void handlePinTakePic() {
  remoteCommandBegin("TAKE_PIC");
  ledOn();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    ledOff();
    sendSerialError("Camera capture failed");
    speakText("Picture failed.");
    remoteCommandEnd(false);
    return;
  }
  imageCount++;
  String filename = "IMG_" + String(imageCount) + ".jpg";
  File file = SD_MMC.open("/images/" + filename, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len); file.close();
    lastCapturedImage = filename;
    refreshImageList();
    currentImageIndex = imageFileList.size() - 1;
    remoteReloadLists = true;
    ledOff();
    sendSerialOK("CAPTURED:" + filename);
    speakText("Picture taken. Image " + String(imageCount) + " saved.");
    remoteCommandEnd(true);
  } else {
    ledOff();
    sendSerialError("SD write failed");
    speakText("Picture failed. Card error.");
    remoteCommandEnd(false);
  }
  esp_camera_fb_return(fb);
  ledShortFlash();
}

// PIN 5: Delete Last Picture
void handlePinDeletePic() {
  remoteCommandBegin("DELETE_PIC");
  if (lastCapturedImage.length() == 0) {
    sendSerialError("No image to delete");
    speakText("No picture to delete.");
    remoteCommandEnd(false);
    return;
  }
  ledLongFlash();
  if (SD_MMC.remove("/images/" + lastCapturedImage)) {
    String deleted = lastCapturedImage;
    lastCapturedImage = "";
    refreshImageList();
    if (!imageFileList.empty()) lastCapturedImage = imageFileList.back();
    remoteReloadLists = true;
    sendSerialOK("DELETED:" + deleted);
    speakText("Picture deleted.");
    remoteCommandEnd(true);
  } else {
    sendSerialError("Delete failed");
    speakText("Delete failed.");
    remoteCommandEnd(false);
  }
}

// PIN 6: Record Audio (default 5 seconds)
void handlePinRecordAudio(int duration = 5) {
  remoteCommandBegin("RECORD_AUDIO");
  if (recordingInProgress) {
    sendSerialError("Recording already in progress");
    speakText("Already recording.");
    remoteCommandEnd(false);
    return;
  }
  ledOn();
  speakText("Recording. " + String(duration) + " seconds.");
  delay(500);
  String filename;
  recordAudioToFile(duration, filename);
  if (filename.length() > 0) {
    remoteReloadLists = true;
    sendSerialOK("RECORDED:" + filename);
    speakText("Recording complete. Audio " + String(audioCount) + " saved.");
    ledShortFlash();
    remoteCommandEnd(true);
  } else {
    sendSerialError("Recording failed");
    speakText("Recording failed.");
    remoteCommandEnd(false);
  }
}

// PIN 7: Delete Last Audio
void handlePinDeleteAudio() {
  remoteCommandBegin("DELETE_AUDIO");
  if (lastRecordedAudio.length() == 0) {
    sendSerialError("No audio to delete");
    speakText("No audio to delete.");
    remoteCommandEnd(false);
    return;
  }
  ledLongFlash();
  if (SD_MMC.remove("/audio/" + lastRecordedAudio)) {
    String deleted = lastRecordedAudio;
    lastRecordedAudio = "";
    refreshAudioList();
    if (!audioFileList.empty()) lastRecordedAudio = audioFileList.back();
    remoteReloadLists = true;
    sendSerialOK("DELETED:" + deleted);
    speakText("Audio deleted.");
    remoteCommandEnd(true);
  } else {
    sendSerialError("Delete failed");
    speakText("Audio delete failed.");
    remoteCommandEnd(false);
  }
}

// PIN 8: Show Next Image (advance index)
void handlePinNextImage() {
  remoteCommandBegin("NEXT_IMAGE");
  refreshImageList();
  if (imageFileList.empty()) {
    sendSerialError("No images");
    speakText("No images available.");
    remoteCommandEnd(false);
    return;
  }
  currentImageIndex = (currentImageIndex + 1) % imageFileList.size();
  String imgName = imageFileList[currentImageIndex];
  int num = currentImageIndex + 1;
  sendSerialOK("IMAGE:" + imgName + "|INDEX:" + String(num) + "|TOTAL:" + String(imageFileList.size()));
  speakText("Image " + String(num) + " of " + String(imageFileList.size()) + ". " + imgName);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN 9: Show Prev Image
void handlePinPrevImage() {
  remoteCommandBegin("PREV_IMAGE");
  refreshImageList();
  if (imageFileList.empty()) {
    sendSerialError("No images");
    speakText("No images available.");
    remoteCommandEnd(false);
    return;
  }
  if (currentImageIndex <= 0) currentImageIndex = imageFileList.size() - 1;
  else currentImageIndex--;
  String imgName = imageFileList[currentImageIndex];
  int num = currentImageIndex + 1;
  sendSerialOK("IMAGE:" + imgName + "|INDEX:" + String(num) + "|TOTAL:" + String(imageFileList.size()));
  speakText("Image " + String(num) + " of " + String(imageFileList.size()) + ". " + imgName);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN 10: Play Next Audio (advance index + play)
void handlePinNextAudio() {
  remoteCommandBegin("NEXT_AUDIO");
  refreshAudioList();
  if (audioFileList.empty()) {
    sendSerialError("No audio");
    speakText("No audio files available.");
    remoteCommandEnd(false);
    return;
  }
  currentAudioIndex = (currentAudioIndex + 1) % audioFileList.size();
  String audName = audioFileList[currentAudioIndex];
  int num = currentAudioIndex + 1;
  sendSerialOK("AUDIO:" + audName + "|INDEX:" + String(num) + "|TOTAL:" + String(audioFileList.size()));
  speakText("Audio " + String(num) + " of " + String(audioFileList.size()) + ".");
  delay(800);
  playWAVSpeaker("/audio/" + audName);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN 11: Play Prev Audio
void handlePinPrevAudio() {
  remoteCommandBegin("PREV_AUDIO");
  refreshAudioList();
  if (audioFileList.empty()) {
    sendSerialError("No audio");
    speakText("No audio files available.");
    remoteCommandEnd(false);
    return;
  }
  if (currentAudioIndex <= 0) currentAudioIndex = audioFileList.size() - 1;
  else currentAudioIndex--;
  String audName = audioFileList[currentAudioIndex];
  int num = currentAudioIndex + 1;
  sendSerialOK("AUDIO:" + audName + "|INDEX:" + String(num) + "|TOTAL:" + String(audioFileList.size()));
  speakText("Audio " + String(num) + " of " + String(audioFileList.size()) + ".");
  delay(800);
  playWAVSpeaker("/audio/" + audName);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN 12: Select Current Image for Export
void handlePinSelectImage() {
  remoteCommandBegin("SELECT_IMAGE");
  refreshImageList();
  if (imageFileList.empty() || currentImageIndex < 0) {
    sendSerialError("No image to select");
    speakText("No image to select.");
    remoteCommandEnd(false);
    return;
  }
  imageForExport = imageFileList[currentImageIndex];
  sendSerialOK("IMAGE_SELECTED:" + imageForExport);
  speakText("Image selected for export. " + imageForExport);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN A0: Select Current Audio for Export
void handlePinSelectAudio() {
  remoteCommandBegin("SELECT_AUDIO");
  refreshAudioList();
  if (audioFileList.empty() || currentAudioIndex < 0) {
    sendSerialError("No audio to select");
    speakText("No audio to select.");
    remoteCommandEnd(false);
    return;
  }
  audioForExport = audioFileList[currentAudioIndex];
  sendSerialOK("AUDIO_SELECTED:" + audioForExport);
  speakText("Audio selected for export. " + audioForExport);
  ledShortFlash();
  remoteCommandEnd(true);
}

// PIN A1: Export (trigger OpenAI analysis on selected pair)
void handlePinExport() {
  remoteCommandBegin("EXPORT");
  if (imageForExport.length() == 0 || audioForExport.length() == 0) {
    sendSerialError("Select image AND audio first");
    speakText("Please select an image and audio file before exporting.");
    remoteCommandEnd(false);
    return;
  }
  if (analysisInProgress) {
    sendSerialError("Analysis already in progress");
    speakText("Analysis already running. Please wait.");
    remoteCommandEnd(false);
    return;
  }
  currentAnalysisID   = String(millis());
  currentImagePath    = "/images/" + imageForExport;
  currentAudioPath    = "/audio/" + audioForExport;
  analysisInProgress  = true;
  analysisResultReady = false;
  analysisProgressStage   = "Starting";
  analysisProgressDetail  = "Remote-triggered analysis...";
  analysisProgressPercent = 0;
  sendSerialOK("ANALYSIS_STARTED|ID:" + currentAnalysisID);
  speakText("Sending to OpenAI. This will take about 90 seconds.");
  delay(100);
  runOpenAIAnalysis();
  remoteCommandEnd(true);
}

// PIN A2: Replay Last TTS Analysis
void handlePinReplayTTS() {
  remoteCommandBegin("REPLAY_TTS");
  if (lastTTSFile.length() == 0 || lastTTSFile == "none") {
    sendSerialError("No TTS available");
    speakText("No analysis available to replay.");
    remoteCommandEnd(false);
    return;
  }
  sendSerialOK("REPLAYING_TTS:" + lastTTSFile);
  speakText("Replaying analysis.");
  // Signal web UI to play
  remoteTTSReady = true;
  remoteTTSFile  = lastTTSFile;
  ledShortFlash();
  remoteCommandEnd(true);
}

// ============================================================================
// MAIN COMMAND DISPATCHER
// ============================================================================
void processSerialCommand(String cmdString) {
  cmdString.trim();
  if (cmdString.length() == 0) return;
  Serial.println("# Processing: " + cmdString);

  // PIN: commands (remote control)
  if (cmdString.startsWith("PIN:") || cmdString.startsWith("CMD:PIN:")) {
    String pinCmd = cmdString;
    if (pinCmd.startsWith("CMD:")) pinCmd = pinCmd.substring(4);
    pinCmd = pinCmd.substring(4); // Remove "PIN:"
    pinCmd.trim();

    if      (pinCmd == "TAKE_PIC")     handlePinTakePic();
    else if (pinCmd == "DELETE_PIC")   handlePinDeletePic();
    else if (pinCmd.startsWith("RECORD_AUDIO")) {
      int dur = 5;
      int colonPos = pinCmd.indexOf(":");
      if (colonPos > 0) dur = pinCmd.substring(colonPos + 1).toInt();
      if (dur <= 0 || dur > 60) dur = 5;
      handlePinRecordAudio(dur);
    }
    else if (pinCmd == "DELETE_AUDIO") handlePinDeleteAudio();
    else if (pinCmd == "NEXT_IMAGE")   handlePinNextImage();
    else if (pinCmd == "PREV_IMAGE")   handlePinPrevImage();
    else if (pinCmd == "NEXT_AUDIO")   handlePinNextAudio();
    else if (pinCmd == "PREV_AUDIO")   handlePinPrevAudio();
    else if (pinCmd == "SELECT_IMAGE") handlePinSelectImage();
    else if (pinCmd == "SELECT_AUDIO") handlePinSelectAudio();
    else if (pinCmd == "EXPORT")       handlePinExport();
    else if (pinCmd == "REPLAY_TTS")   handlePinReplayTTS();
    else sendSerialError("Unknown PIN command: " + pinCmd);
    return;
  }

  // CMD: commands (original v1.0.0)
  String command = extractCommand(cmdString);
  if (command.length() == 0) { sendSerialError("Invalid command format. Use CMD:command or PIN:command"); return; }

  if      (command == "PING")           handleSerialPing();
  else if (command == "VERSION")        handleSerialVersion();
  else if (command == "STATUS")         handleSerialStatus();
  else if (command == "CAPTURE")        handleSerialCapture();
  else if (command == "RECORD") {
    String durationStr = "";
    int colonPos = cmdString.indexOf(":", cmdString.indexOf("CMD:") + 4);
    if (colonPos > 0) durationStr = cmdString.substring(colonPos + 1);
    handleSerialRecord(durationStr.toInt());
  }
  else if (command == "LIST_IMAGES")    handleSerialListImages();
  else if (command == "LIST_AUDIO")     handleSerialListAudio();
  else if (command == "GET_LAST_IMAGE") handleSerialGetLastImage();
  else if (command == "GET_LAST_AUDIO") handleSerialGetLastAudio();
  else if (command == "DIAG")           handleSerialDiag();
  else sendSerialError("Unknown command: " + command);
}

void handleSerialCommands() {
  while (UNOSerial.available() > 0) {
    char c = UNOSerial.read();
    serialTotalRx++;
    serialLastActivityMs = millis();
    serialPortActive     = true;

    if (c == '\n' || c == '\r') {
      if (serialCommandBuffer.length() > 0) { serialCommandReady = true; break; }
    } else {
      serialCommandBuffer += c;
      if (serialCommandBuffer.length() > 256) {
        serialCommandBuffer = "";
        sendSerialError("Command too long");
      }
    }
  }

  // Mark port inactive after 5 seconds of silence
  if (serialPortActive && (millis() - serialLastActivityMs > 5000)) {
    serialPortActive = false;
  }

  if (serialCommandReady) {
    serialCmdCount++;
    Serial.println("RX: " + serialCommandBuffer);  // mirror to USB monitor
    serialLogPush(">> " + serialCommandBuffer);
    processSerialCommand(serialCommandBuffer);
    serialCommandBuffer = "";
    serialCommandReady  = false;
  }
}

// ============================================================================
// DIAGNOSTICS ENDPOINTS (v1.2.18 / v1.2.19)
// ============================================================================
void handleDiagnostics() {
  if (!diagAvailable) {
    server.send(200, "application/json", "{\"available\":false,\"message\":\"No diagnostics data yet. Run an analysis or /test/pipeline first.\"}");
    return;
  }
  const PipelineTiming& d = lastDiag;
  String json = "{";
  json += "\"available\":true,";
  json += "\"runID\":\"" + d.runID + "\",";
  json += "\"mode\":\"" + String(d.isSynthetic ? "synthetic" : "live") + "\",";
  json += "\"completed\":" + String(d.completed ? "true" : "false") + ",";
  json += "\"errorStage\":\"" + d.errorStage + "\",";
  json += "\"timestamps\":{";
  json += "\"t0_taskStart\":"      + String(d.t0_taskStart) + ",";
  json += "\"t1_imgOpened\":"      + String(d.t1_imgOpened) + ",";
  json += "\"t2_b64Done\":"        + String(d.t2_b64Done) + ",";
  json += "\"t3_visionPost\":"     + String(d.t3_visionPost) + ",";
  json += "\"t4_visionResponse\":" + String(d.t4_visionResponse) + ",";
  json += "\"t5_visionParsed\":"   + String(d.t5_visionParsed) + ",";
  json += "\"t6_ttsPost\":"        + String(d.t6_ttsPost) + ",";
  json += "\"t7_ttsFB\":"          + String(d.t7_ttsFB) + ",";
  json += "\"t8_ttsStreamDone\":"  + String(d.t8_ttsStreamDone) + ",";
  json += "\"t9_sdWriteDone\":"    + String(d.t9_sdWriteDone) + ",";
  json += "\"t10_flagSet\":"       + String(d.t10_flagSet) + ",";
  json += "\"t11_playStart\":"     + String(d.t11_playStart) + ",";
  json += "\"t12_playDone\":"      + String(d.t12_playDone);
  json += "},\"deltas\":{";
  json += "\"ms_b64\":"        + String(d.ms_b64()) + ",";
  json += "\"ms_vision\":"     + String(d.ms_vision()) + ",";
  json += "\"ms_ttsFB\":"      + String(d.ms_ttsfb()) + ",";
  json += "\"ms_ttsStream\":"  + String(d.ms_ttsstream()) + ",";
  json += "\"ms_sdWrite\":"    + String(d.ms_sdwrite()) + ",";
  json += "\"ms_uiHold\":"     + String(d.ms_uihold()) + ",";
  json += "\"ms_play\":"       + String(d.ms_play()) + ",";
  json += "\"ms_total\":"      + String(d.ms_total());
  json += "},\"metrics\":{";
  json += "\"imgFileBytes\":"     + String(d.imgFileBytes) + ",";
  json += "\"b64Chars\":"         + String(d.b64Chars) + ",";
  json += "\"audioFileBytes\":"   + String(d.audioFileBytes) + ",";
  json += "\"pcmBytesBuffered\":" + String(d.pcmBytesBuffered) + ",";
  json += "\"ttsWavBytes\":"      + String(d.ttsWavBytes) + ",";
  json += "\"heapFreeStart\":"    + String(d.heapFreeStart) + ",";
  json += "\"psramFreeStart\":"   + String(d.psramFreeStart) + ",";
  json += "\"psramFreeAfter\":"   + String(d.psramFreeAfterBuf) + ",";
  json += "\"wifiRSSI\":"         + String((int)d.wifiRSSI) + ",";
  json += "\"apiKeyLen\":"        + String((int)d.apiKeyLen);
  json += "}}";
  server.send(200, "application/json", json);
}

void handleDiagClear() {
  lastDiag = PipelineTiming();
  diagAvailable = false;
  server.send(200, "application/json", "{\"cleared\":true}");
}

// Synthetic pipeline test: bypasses camera capture and microphone recording.
// Generates a minimal valid JPEG (64x64 solid grey, ~2KB) and uses a hardcoded
// transcription string so every real processing stage is exercised without
// needing physical input devices.
void handleTestPipeline() {
  if (analysisInProgress) {
    server.send(409, "application/json", "{\"error\":\"Analysis already in progress\"}");
    return;
  }

  Serial.println("\n[TEST] Synthetic pipeline test triggered");
  // -- Pre-flight: verify API key health before spending network time ---------
  String keyCheck = sanitizeAPIKey(SettingsStore::getOpenAIKey());
  if (keyCheck.length() < 10 || !keyCheck.startsWith("sk-")) {
    Serial.println("[TEST] ABORT: API key is not configured or invalid. Set key via /settings.");
    server.send(400, "application/json", "{\"error\":\"API key not configured. Visit /settings to set your OpenAI key.\"}");
    return;
  }

  analysisInProgress = true;

  // -- Capture a real frame from the camera and save to SD -------------------
  // Using esp_camera_fb_get() guarantees a proper JPEG that GPT-4 Vision will
  // accept. The camera is already initialized by setup(). This also tests the
  // actual capture path so the image size in the diag reflects real-world data.
  // We release the framebuffer immediately after writing to SD to return PSRAM.
  const String synImgPath = "/images/TEST_IMG.jpg";

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[TEST] ERROR: Camera framebuffer capture failed");
    analysisInProgress = false; return;
  }
  File imgF = SD_MMC.open(synImgPath, FILE_WRITE);
  if (!imgF) {
    esp_camera_fb_return(fb);
    Serial.println("[TEST] ERROR: Cannot write test image to SD");
    analysisInProgress = false; return;
  }
  imgF.write(fb->buf, fb->len);
  imgF.close();
  Serial.printf("[TEST] Camera frame captured and saved: %s (%d bytes)\n",
                synImgPath.c_str(), (int)fb->len);
  esp_camera_fb_return(fb);
  fb = nullptr;

  // -- Set up globals ---------------------------------------------------------
  currentImagePath   = synImgPath;
  currentAudioPath   = "";
  currentAnalysisID  = "TEST_" + String(millis());

  // -- Init timing struct -----------------------------------------------------
  PipelineTiming diag;
  diag.runID          = currentAnalysisID;
  diag.isSynthetic    = true;
  diag.t0_taskStart   = millis();
  diag.heapFreeStart  = (uint32_t)ESP.getFreeHeap();
  diag.psramFreeStart = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  diag.wifiRSSI       = (int8_t)WiFi.RSSI();
  diag.apiKeyLen      = (uint8_t)min((int)keyCheck.length(), 255);

  // Stage: Vision
  analysisProgressStage   = "Vision (Synthetic)";
  analysisProgressDetail  = "Running GPT-4 Vision on camera frame...";
  analysisProgressPercent = 40;
  server.handleClient(); yield();

  const String synTranscription = "This is an automated pipeline test. Describe what you see in this image in one sentence.";
  String analysis = analyzeImageWithGPT4Vision(synImgPath, synTranscription, &diag);
  if (analysis.startsWith("Error")) {
    diag.errorStage = "Vision: " + analysis;
    lastDiag = diag; diagAvailable = true;
    printDiagTable(lastDiag);
    analysisProgressStage = "Error"; analysisProgressDetail = analysis;
    analysisInProgress = false; return;
  }

  // Stage: TTS
  analysisProgressStage   = "TTS (Synthetic)";
  analysisProgressDetail  = "Generating speech for synthetic result...";
  analysisProgressPercent = 75;
  server.handleClient(); yield();

  String ttsFile = generateTTSAudio(analysis, currentAnalysisID, &diag);
  if (ttsFile.length() == 0) ttsFile = "none";
  lastTTSFile = ttsFile;

  analysisProgressStage   = "Complete";
  analysisProgressDetail  = "Synthetic test complete";
  analysisProgressPercent = 100;

  analysisResultText    = "[SYNTHETIC TEST]\n\n" + analysis;
  analysisResultTTSFile = ttsFile;
  analysisResultSuccess = true;
  analysisResultReady   = true;
  analysisInProgress    = false;

  // v1.2.36: TTS plays in browser -- no deferred speaker trigger
  diag.t10_flagSet = millis();

  diag.completed = true;
  lastDiag = diag; diagAvailable = true;
  printDiagTable(lastDiag);
  Serial.println("[TEST] Synthetic pipeline complete. Fetch /diagnostics for JSON results.");
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);  // USB CDC monitor
  UNOSerial.begin(UNO_BAUD, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);  // Gravity -> UNO
  delay(1000);

  // Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  ledOff();

  Serial.println("\n\n==========================================");
  Serial.println("ESP32-S3 AI CAM - Automation Agent v" FIRMWARE_VERSION);
  Serial.println("Built: " FIRMWARE_BUILD_DATE " " FIRMWARE_BUILD_TIME);
  Serial.println("==========================================\n");

  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("ERROR: SD Card Mount Failed");
    return;
  }
  Serial.println("SD Card OK");
  SD_MMC.mkdir("/images");
  SD_MMC.mkdir("/audio");
  SD_MMC.mkdir("/tts");
  SD_MMC.mkdir("/analysis");

  setupCamera();
  Serial.println("Camera OK");

  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("PDM init failed!");
  } else {
    Serial.println("PDM Microphone OK");
  }

  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Speaker init failed!");
  } else {
    Serial.println("Speaker OK");
  }

  // Settings store -- loads from NVS, falls back to DEFAULT_* on first flash
  Serial.println("\n=== Settings Store ===");
  SettingsStore::begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD,
                       DEFAULT_OPENAI_API_KEY, DEFAULT_DEVICE_NAME);

  Serial.println("\n=== WiFi Manager ===");
  if (!WiFiManager::connect(
    SettingsStore::getWiFiSSID().c_str(),
    SettingsStore::getWiFiPassword().c_str()
  )) {
    Serial.println("ERROR: WiFi failed");
    while(1) { delay(1000); }
  }

  // Initialize file lists
  refreshImageList();
  refreshAudioList();
  refreshTTSList();

  // Web routes
  server.on("/",                HTTP_GET,    []() { server.send_P(200, "text/html", index_html); });
  server.on("/stream",          HTTP_GET,    handleStream);
  server.on("/capture",         HTTP_GET,    handleCapture);
  server.on("/image",           HTTP_GET,    handleImageView);
  server.on("/image/latest",    HTTP_GET,    handleLatestImage);
  server.on("/audio/record",    HTTP_GET,    handleAudioRecord);
  server.on("/audio",           HTTP_GET,    handleAudioStream);
  server.on("/audio/speaker",   HTTP_GET,    handleSpeakerPlayback);
  server.on("/tts",             HTTP_GET,    handleTTSStream);
  server.on("/tts/speaker",     HTTP_GET,    handleTTSSpeaker);
  server.on("/list/images",     HTTP_GET,    handleListImages);
  server.on("/list/audio",      HTTP_GET,    handleListAudio);
  server.on("/list/tts",        HTTP_GET,    handleListTTS);
  server.on("/delete/image",    HTTP_DELETE, handleDeleteImage);
  server.on("/delete/audio",    HTTP_DELETE, handleDeleteAudio);
  server.on("/delete/tts",      HTTP_DELETE, handleDeleteTTS);
  server.on("/openai/analyze",  HTTP_POST,   handleOpenAIAnalyze);
  server.on("/openai/progress", HTTP_GET,    handleOpenAIProgress);
  server.on("/analysis/download",HTTP_POST,  handleAnalysisDownload);
  server.on("/analysis/package", HTTP_GET,   handleAnalysisPackage);
  server.on("/analysis/export",  HTTP_GET,   handleAnalysisExport);
  server.on("/analysis/file",    HTTP_GET,   handleAnalysisFile);
  server.on("/remote/status",    HTTP_GET,   handleRemoteStatus);
  server.on("/serial/diag",      HTTP_GET,   handleSerialDiagEndpoint);
  server.on("/diagnostics",      HTTP_GET,   handleDiagnostics);
  server.on("/diag/clear",       HTTP_DELETE,handleDiagClear);
  server.on("/test/pipeline",    HTTP_POST,  handleTestPipeline);

  // /serial/cmd -- lets the web UI trigger any serial command directly
  server.on("/serial/cmd", HTTP_POST, []() {
    DynamicJsonDocument req(256);
    deserializeJson(req, server.arg("plain"));
    String cmd = req["cmd"].as<String>();
    cmd.trim();
    if (cmd.length() > 0) {
      serialLogPush("[WEB] " + cmd);
      serialCmdCount++;
      processSerialCommand(cmd);
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty cmd\"}");
    }
  });

  server.on("/settings",         HTTP_GET,   handleSettingsPage);
  server.on("/settings/save",    HTTP_POST,  handleSettingsSave);

  server.on("/system/status", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
    doc["wifiConnected"]   = WiFi.isConnected();
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["freeHeap"]        = ESP.getFreeHeap();
    doc["uptime"]          = millis() / 1000;
    doc["imageCount"]      = (int)imageFileList.size();
    doc["audioCount"]      = (int)audioFileList.size();
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.begin();

  String localIP = WiFi.localIP().toString();
  String apiKeyStatus = (SettingsStore::getOpenAIKey().startsWith("sk-YOUR") ||
                         SettingsStore::getOpenAIKey().length() < 10) ? "NOT SET!" : "Configured";

  Serial.println("\n");
  Serial.println("****************************************************");
  Serial.println("*                                                  *");
  Serial.println("*      AI CAMERA AUTOMATION AGENT v1.2.41         *");
  Serial.println("*                                                  *");
  Serial.printf( "*  --> http://%-36s*\n", (localIP + "  ").c_str());
  Serial.println("*                                                  *");
  Serial.printf( "*  Settings: http://%-31s*\n", (localIP + "/settings  ").c_str());
  Serial.println("*                                                  *");
  Serial.println("*  Serial API: ENABLED (9600 baud - Gravity)          *");
  Serial.println("*  Remote UNO: GPIO44(RX) GPIO43(TX)                 *");
  Serial.println("****************************************************\n");
  Serial.printf("Firmware   : v%s\n", FIRMWARE_VERSION);
  Serial.printf("Device     : %s\n", SettingsStore::getDeviceName().c_str());
  Serial.printf("IP Address : %s\n", localIP.c_str());
  Serial.printf("API Key    : %s\n", apiKeyStatus.c_str());
  Serial.printf("Images     : %d\n", (int)imageFileList.size());
  Serial.printf("Audio      : %d\n", (int)audioFileList.size());
  Serial.println("\nSerial Commands:");
  Serial.println("  CMD:PING  CMD:STATUS  CMD:CAPTURE  CMD:RECORD:5");
  Serial.println("  CMD:DIAG  (full diagnostic report + functional check)");
  Serial.println("  PIN:TAKE_PIC  PIN:RECORD_AUDIO:5  PIN:NEXT_IMAGE");
  Serial.println("  PIN:SELECT_IMAGE  PIN:SELECT_AUDIO  PIN:EXPORT");
  Serial.println("  PIN:REPLAY_TTS  (and more - see documentation)\n");
  Serial.println("Ready.\n");

  // Startup announcement
  delay(500);
  speakText("AI Camera ready. Automation Agent version 1.2.41 online.");

  // v1.2.41 -- send ONLINE to UNO immediately so it knows the ESP32 is up.
  // The UNO boots in ~2s; the ESP32 takes 20-30s. Without this the UNO
  // finishes its startup and sits silent before the ESP32 is ready.
  // loop() will repeat this every UNO_SYNC_INTERVAL_MS for UNO_SYNC_DURATION_MS.
  String onlineMsg = "ONLINE|VER:" + String(FIRMWARE_VERSION) +
                     "|IP:" + WiFi.localIP().toString();
  UNOSerial.println(onlineMsg);
  Serial.println("[UNO] " + onlineMsg);
  serialLogPush("[UNO] " + onlineMsg);
  unoSyncLastMs = millis();
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  server.handleClient();
  WiFiManager::monitor();
  handleSerialCommands();

  // v1.2.41 -- repeat ONLINE every UNO_SYNC_INTERVAL_MS for the first
  // UNO_SYNC_DURATION_MS after boot so the UNO reliably detects the ESP32
  // regardless of which device powered on first or received new firmware.
  if (!unoSyncComplete) {
    unsigned long now = millis();
    if (now >= UNO_SYNC_DURATION_MS) {
      unoSyncComplete = true;  // 30 s window expired -- stop repeating
    } else if (now - unoSyncLastMs >= UNO_SYNC_INTERVAL_MS) {
      unoSyncLastMs = now;
      String onlineMsg = "ONLINE|VER:" + String(FIRMWARE_VERSION) +
                         "|IP:" + WiFi.localIP().toString();
      UNOSerial.println(onlineMsg);
      Serial.println("[UNO] " + onlineMsg);
      serialLogPush("[UNO] " + onlineMsg);
    }
  }

  // Serial heartbeat -- emitted every 5 seconds so UNO (and Serial Monitor)
  // can confirm the ESP32 is alive and the serial link is working.
  if (millis() - serialHeartbeatMs >= 5000) {
    serialHeartbeatMs = millis();
    String hb = "HB|UPTIME:" + String(millis()/1000) +
                "|HEAP:"     + String(ESP.getFreeHeap()) +
                "|IMGS:"     + String(imageFileList.size()) +
                "|AUD:"      + String(audioFileList.size()) +
                "|STATE:"    + String(analysisInProgress ? "ANALYZING" :
                                      recordingInProgress ? "RECORDING" : "READY");
    UNOSerial.println(hb);   // to UNO
    Serial.println(hb);      // mirror to USB monitor
    serialLogPush("[HB] " + hb);
    serialTotalTx += hb.length() + 2;
  }

  yield();
}

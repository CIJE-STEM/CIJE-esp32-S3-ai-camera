/*
 * ESP32-S3 AI CAM Serial API Test
 * 
 * This sketch tests the serial command interface of the ESP32-S3 AI CAM
 * Can be uploaded to Arduino UNO or used via Serial Monitor
 * 
 * Hardware Connections:
 * Arduino UNO    →    ESP32-S3
 * TX (Pin 1)     →    RX (depends on your ESP32 board)
 * RX (Pin 0)     →    TX (depends on your ESP32 board)
 * GND            →    GND
 * 
 * NOTE: For initial testing, just use the ESP32's Serial Monitor!
 */

void setup() {
  Serial.begin(115200);
  
  // Wait for serial port to connect
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("\n========================================");
  Serial.println("ESP32-S3 AI CAM - Serial API Tester");
  Serial.println("========================================\n");
  
  delay(2000); // Give ESP32 time to boot
  
  // Run test sequence
  runTests();
}

void loop() {
  // After tests, enter interactive mode
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.length() > 0) {
      Serial.println("\n>>> Sending: " + input);
      Serial.println(input); // Send to ESP32
      delay(100);
      
      // Wait for response
      unsigned long startTime = millis();
      while (millis() - startTime < 5000) { // 5 second timeout
        if (Serial.available()) {
          String response = Serial.readStringUntil('\n');
          Serial.println("<<< Response: " + response);
          break;
        }
        delay(10);
      }
    }
  }
}

void sendCommand(String cmd) {
  Serial.println("\n>>> " + cmd);
  Serial.println(cmd);
  delay(100);
  
  // Wait for response
  unsigned long startTime = millis();
  bool gotResponse = false;
  
  while (millis() - startTime < 5000) { // 5 second timeout
    if (Serial.available()) {
      String response = Serial.readStringUntil('\n');
      if (response.startsWith("OK:") || response.startsWith("ERROR:")) {
        Serial.println("<<< " + response);
        gotResponse = true;
        break;
      } else if (response.startsWith("#")) {
        // Debug echo from ESP32
        Serial.println("    " + response);
      }
    }
    delay(10);
  }
  
  if (!gotResponse) {
    Serial.println("<<< TIMEOUT (no response)");
  }
  
  delay(500); // Pause between commands
}

void runTests() {
  Serial.println("Starting automated test sequence...\n");
  delay(1000);
  
  // Test 1: PING
  Serial.println("TEST 1: Connection Test");
  sendCommand("CMD:PING");
  
  // Test 2: VERSION
  Serial.println("\nTEST 2: Version Check");
  sendCommand("CMD:VERSION");
  
  // Test 3: STATUS
  Serial.println("\nTEST 3: System Status");
  sendCommand("CMD:STATUS");
  
  // Test 4: LIST_IMAGES
  Serial.println("\nTEST 4: List Images");
  sendCommand("CMD:LIST_IMAGES");
  
  // Test 5: LIST_AUDIO
  Serial.println("\nTEST 5: List Audio Files");
  sendCommand("CMD:LIST_AUDIO");
  
  // Test 6: CAPTURE
  Serial.println("\nTEST 6: Capture Image");
  Serial.println("(This will take a photo)");
  sendCommand("CMD:CAPTURE");
  
  // Test 7: GET_LAST_IMAGE
  Serial.println("\nTEST 7: Get Last Captured Image");
  sendCommand("CMD:GET_LAST_IMAGE");
  
  // Test 8: RECORD
  Serial.println("\nTEST 8: Record Audio (5 seconds)");
  Serial.println("(This will record 5 seconds of audio)");
  Serial.println("(Wait for it to complete...)");
  sendCommand("CMD:RECORD:5");
  delay(5500); // Wait for recording to complete
  
  // Test 9: GET_LAST_AUDIO
  Serial.println("\nTEST 9: Get Last Recorded Audio");
  sendCommand("CMD:GET_LAST_AUDIO");
  
  // Test 10: Invalid command
  Serial.println("\nTEST 10: Invalid Command Test");
  sendCommand("CMD:INVALID_TEST");
  
  // Test complete
  Serial.println("\n========================================");
  Serial.println("All tests complete!");
  Serial.println("========================================");
  Serial.println("\nYou can now enter commands manually.");
  Serial.println("Type commands like: CMD:PING");
  Serial.println("Press Enter to send.\n");
}

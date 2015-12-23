#define DEBUG_HTTPCLIENT(...) Serial.printf( __VA_ARGS__ )

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <PN532.h>
#include <PN532_SPI.h>

#define VERSION "0.1"
#define SSID "***REMOVED***"
#define KEY "***REMOVED***"
#define HTTP_HOST "192.168.1.108"
#define HTTP_PORT 1337

PN532_SPI pn532spi(SPI, 15);
PN532 nfc(pn532spi);

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("ESP8266 MiFare NFC Reader v" VERSION " - Denver Abrey [denvera@gmail.com]");
  Serial.setDebugOutput(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, KEY);
  Serial.println("Trying to connect to SSID: " SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Init NFC...");
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }
  
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // configure board to read RFID tags
  nfc.SAMConfig();
  
  Serial.println("Waiting for an ISO14443A Card ...");

}

JsonObject * getTagInfo(uint8_t uid[7], uint8_t uidLength, JsonBuffer *jsonBuffer) {
  Serial.print("Get /tag/");
  HTTPClient http;  
  String uidStr;
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] <= 0xf) uidStr += "0";
    uidStr += String(uid[i], HEX);
  }
  Serial.println(uidStr);
  http.begin(HTTP_HOST, HTTP_PORT, "/tags/" + uidStr);
  int httpCode = http.GET();
  JsonObject * root = NULL;
  Serial.print("HTTP Code: "); Serial.print(httpCode, DEC);
  Serial.println("");
  if (httpCode) {
    if (httpCode == HTTP_CODE_OK) {
      String resp = http.getString();
      Serial.println("OK - " + resp);
    
      //StaticJsonBuffer<200> jsonBuffer;
      root = &(jsonBuffer->parseObject(resp));      
      return root;
      } else {
        Serial.printf("[HTTP] GET failed");
      }
  }  
  return root;
}

bool verifyBlock(uint8_t sector, uint8_t keyN, uint8_t uid[7], uint8_t uidLen, uint8_t key[6], const char * content) {
  uint8_t block = sector / 4;  
  bool success = false;
  success = nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, keyN, key);
  if (success) {
    Serial.print("Authenticated to block "); Serial.println(block, DEC);
    unsigned char data[16];
    success = nfc.mifareclassic_ReadDataBlock(block, data);
    if (String(content).equals(String((char *)data))) {
      Serial.println("Content matched!");
      return true;
    } else {
      Serial.print("Content not matched: "); Serial.print(String((char *)data)); Serial.print(" != "); Serial.println(content);
      nfc.PrintHexChar(data, 16);
      Serial.println("---");
      return false;
    }
  } else {
    Serial.print("Failed to authenticate to block "); Serial.println(block, DEC);
    return false;
  }  
}

bool processTagData(JsonObject& blocks, uint8_t uid[7], uint8_t uidLength) {
  Serial.println("Attempting to verify blocks...");
  //blocks.prettyPrintTo(Serial);
  for (JsonObject::iterator it=blocks.begin(); it!=blocks.end(); ++it)
  {
   Serial.println(it->key);
   //String content = "";
   const char * content;
   if (((JsonObject&)(it->value)).containsKey("Content")) {
    content = (const char *) (((JsonObject&)(it->value))["Content"]);
    Serial.println("Match Content: " + String(content));
   } else {
    Serial.println("No content, can't verify");
    return false;
   }
   uint8_t keyN = -1;   
   uint8_t key[6];
   if (((JsonObject&)(it->value)).containsKey("KeyA")) {
    Serial.println("Found KeyA");
    keyN = 0;        
   } else if (((JsonObject&)(it->value)).containsKey("KeyB")) {
    Serial.println("Found KeyB");
    keyN = 1;    
   }
   Serial.print("Key: ");
   for (uint8_t i = 0; i<6; i++) {
    key[i] = (keyN == 0) ? (uint8_t) (((JsonObject&)(it->value))["KeyA"][i]) : (uint8_t) (((JsonObject&)(it->value))["KeyB"][i]);
    Serial.print(key[i], HEX);
    Serial.print(" ");
   }
   Serial.println("");
   uint8_t sector = String((const char *) (it->key)).toInt();
   verifyBlock(sector, keyN, uid, uidLength, key, content);
   
  }
}



void loop() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
    
  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success) {
    // Display some basic information about the card
    Serial.println("Found an ISO14443A card");
    Serial.print("  UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
    Serial.print("  UID Value: ");
    nfc.PrintHex(uid, uidLength);
    Serial.println("");
    
    if (uidLength == 4)
    {
      // We probably have a Mifare Classic card ... 
      Serial.println("Seems to be a Mifare Classic card (4 byte UID)");
    
      // Now we need to try to authenticate it for read/write access
      // Try with the factory default KeyA: 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
      Serial.println("Trying to authenticate block 4 with default KEYA value");
      StaticJsonBuffer<1024> jsonBuffer;
      JsonObject * tagData = getTagInfo(uid, uidLength, &jsonBuffer);
      if (tagData == NULL) {
        Serial.println("Didn't get any tag data :("); 
      } else {
        tagData->prettyPrintTo(Serial);
        bool valid = ((JsonObject&)(*tagData))["valid"];
        if (valid) {
          String comment = ((JsonObject&)(*tagData))["comment"];
          Serial.println("Attempting to read blocks from tag: " + comment);
          processTagData((*tagData)["blocks"], uid, uidLength);
        } else {
          Serial.println("Tag flagged invalid");
        }
      }
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    
    // Start with block 4 (the first block of sector 1) since sector 0
    // contains the manufacturer data and it's probably better just
    // to leave it alone unless you know what you're doing
      success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya);
    
      if (success)
      {
        Serial.println("Sector 1 (Blocks 4..7) has been authenticated");
        uint8_t data[16];
    
        // If you want to write something to block 4 to test with, uncomment
    // the following line and this text should be read back in a minute
        // data = { 'a', 'd', 'a', 'f', 'r', 'u', 'i', 't', '.', 'c', 'o', 'm', 0, 0, 0, 0};
        // success = nfc.mifareclassic_WriteDataBlock (4, data);

        // Try to read the contents of block 4
        success = nfc.mifareclassic_ReadDataBlock(4, data);
    
        if (success)
        {
          // Data seems to have been read ... spit it out
          Serial.println("Reading Block 4:");
          nfc.PrintHexChar(data, 16);
          Serial.println("");
      
          // Wait a bit before reading the card again
          delay(1000);
        }
        else
        {
          Serial.println("Ooops ... unable to read the requested block.  Try another key?");
        }
      }
      else
      {
        Serial.println("Ooops ... authentication failed: Try another key?");
      }
    }
    
    if (uidLength == 7)
    {
      // We probably have a Mifare Ultralight card ...
      Serial.println("Seems to be a Mifare Ultralight tag (7 byte UID)");
    
      // Try to read the first general-purpose user page (#4)
      Serial.println("Reading page 4");
      uint8_t data[32];
      success = nfc.mifareultralight_ReadPage (4, data);
      if (success)
      {
        // Data seems to have been read ... spit it out
        nfc.PrintHexChar(data, 4);
        Serial.println("");
    
        // Wait a bit before reading the card again
        delay(1000);
      }
      else
      {
        Serial.println("Ooops ... unable to read the requested page!?");
      }
    }
  }  

}

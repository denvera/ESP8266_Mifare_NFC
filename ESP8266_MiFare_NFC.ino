#define DEBUG_HTTPCLIENT(...) Serial.printf( __VA_ARGS__ )

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <SPI.h>
#include <PN532.h>
#include <PN532_SPI.h>

#define VERSION "0.2"
#define JSON_BUFSIZE 1024

// Add your config below, or just add them to config.h in the same directory and uncomment #define EXTCONFIG below
// config.h
#define SSID "YourSSID"
#define KEY "YourKey"
#define HTTP_HOST "x.x.x.x"
#define HTTP_PORT 1337
// End config.h


//#define EXTCONFIG
#ifdef  EXTCONFIG
#include "config.h"
#endif

PN532_SPI pn532spi(SPI, 15);
PN532 nfc(pn532spi);
ESP8266WebServer server(80);
bool mounted = false;

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
    while (1); // halt, will force restart after WDT timeout
  }
  
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // configure board to read RFID tags
  nfc.SAMConfig();
  if (SPIFFS.begin()) {
    mounted = true;
    Serial.println("Mounted SPIFFS");
  } else {
    Serial.println("Failed to mount SPIFFS");
  }
  Serial.println("Waiting for an ISO14443A Card ...");

}

JsonObject * getTagInfoFromFS(String uidStr) {
  if (mounted) {
    if (SPIFFS.exists("/tags/" + uidStr + ".txt")) {
      Serial.println("Found tag file");
      File f = SPIFFS.open("/tags/" + uidStr + ".txt", "r");
      if (f) {        
        String tagJson = f.readString();
        f.close();
        JsonObject * root = NULL;
        StaticJsonBuffer<200> jsonBuffer;
        root = &(jsonBuffer.parseObject(tagJson));
        if (root->success()) {
          return root;
        } else {
          Serial.println("Error parsing json: " + tagJson);
          return NULL;
        }
      } else {
        Serial.println("Couldnt open tag file");
      }
    } else {
      Serial.println("No tag file found");
    }
  }
  return NULL;
}

bool saveTagInfoToFS(String uidStr, String tagJson) {
  File f = SPIFFS.open("/tags/" + uidStr + ".txt", "w+");
  if (!f) {
    Serial.println("file open failed");
  } else {
     f.println(tagJson);
     f.close();
     Serial.println("Saved tag info");
  }
}

JsonObject * getTagInfo(uint8_t uid[7], uint8_t uidLength) {
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
  http.setTimeout(100);
  JsonObject * root = NULL;
  Serial.print("HTTP Code: "); Serial.print(httpCode, DEC);
  Serial.println("");
  if (httpCode) {
    if (httpCode == HTTP_CODE_OK) {
      String resp = http.getString();
      Serial.println("OK - " + resp);
    
      StaticJsonBuffer<JSON_BUFSIZE> jsonBuffer;
      root = &(jsonBuffer.parseObject(resp));         
      if (root->success()) {
        saveTagInfoToFS(uidStr, resp);
        return root;           
      } else {
        return NULL;        
      }
      return root;
      } else {
        Serial.println("[HTTP] GET failed, trying SPIFFS");
        root = getTagInfoFromFS(uidStr);
      }
  }  
  return root;
}

bool verifyBlock(uint8_t block, uint8_t keyN, uint8_t uid[7], uint8_t uidLen, uint8_t key[6], const char * content) {
  uint8_t sector = block / 4;  
  bool success = false;
  success = nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, keyN, key);
  if (success) {
    Serial.print("Authenticated to block "); Serial.println(block, DEC);
    unsigned char data[16];
    success = nfc.mifareclassic_ReadDataBlock(block, data);
    //if (String(content).equals(String((char *)data))) {
    if (memcmp(content, data, 16) == 0) {
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
  bool verified=false;
  for (JsonObject::iterator it=blocks.begin(); it!=blocks.end(); ++it)
  {
   Serial.print("Block: ");
   Serial.println(it->key);
   //String content = "";
   char content[16];
   if (((JsonObject&)(it->value)).containsKey("Content")) {
    //content = (const char *) (((JsonObject&)(it->value))["Content"]);
    //Serial.println("Match Content: " + String(content));
    Serial.println("Match Content: ");
    for (uint8_t i = 0; i < 16; i++) {
      content[i] = (uint8_t)((JsonObject&)(it->value))["Content"][i];
      Serial.print(content[i], HEX);      
      Serial.print(" ");
    }
    Serial.println("");
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
   uint8_t block = String((const char *) (it->key)).toInt();
   verified = verifyBlock(block, keyN, uid, uidLength, key, content);
   if (!verified) {    
    Serial.println("Block didn't match!");
    //return false;
   }
   
  }
  return verified;
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

      //StaticJsonBuffer<1024> jsonBuffer;
      JsonObject * tagData = getTagInfo(uid, uidLength);
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
      delay(200);
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

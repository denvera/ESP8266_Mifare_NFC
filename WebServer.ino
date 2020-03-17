const char html1[] PROGMEM = "<html><head><title>MifareNFC Door</title></head> \
    <body> \
        <div id=\"open\"> \
    <form action=\"/door\" method=\"post\"> \
    <input type=\"hidden\" name=\"d\" value=\"open\"> \
    <input type=\"submit\" value=\"Open Door\"> \
    </form> \
    </div> \
    <p align=\"center\">Uptime: ";
const char html2[] PROGMEM = "</p> \
    </body> \
</html>";

void ICACHE_FLASH_ATTR handleRoot() {
  char temp[48];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  sprintf(temp, "%02d:%02d:%02d", hr, min % 60, sec % 60);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send ( 200, "text/html", "");
  server.sendContent(html1);    
  server.sendContent(temp);
  server.sendContent(html2);
  server.sendContent("");
  //server.send(200, "text/plain", "Im a door");
}

void ICACHE_FLASH_ATTR handleSetDoor() {
  if (server.hasArg("d")) {
    if (server.arg("d") == "open") {
      // Open Door
      tagSuccess();
      server.send(200, "text/plain", "Opening Door");
    } else {
      server.send(200, "text/plain", "Request not understood");
    }
  }
}
void ICACHE_FLASH_ATTR handleGetDoor() {
  server.send(200, "text/plain", "I'm a door");
}


void ICACHE_FLASH_ATTR setupWebServer() {
  server.on ("/", handleRoot);
  server.on("/door", HTTP_GET, handleGetDoor);
  server.on("/door", HTTP_POST, handleSetDoor);

}

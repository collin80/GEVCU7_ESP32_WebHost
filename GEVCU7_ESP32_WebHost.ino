#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
    client->text("PONG BRO!");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}


const char* ssid = "KidderWiFi";
const char* password = "Pony9367Buggy";
const char * hostName = "gevcu7";
const char* http_username = "admin";
const char* http_password = "admin";

void setup(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  //WiFi.mode(WIFI_AP_STA);
  //WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }

  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();

  MDNS.begin("gevcu7");
  MDNS.addService("http","tcp",80);

  Serial.println("About to start SPIFFS");

  SPIFFS.begin(false, "/spiffs", 20);

  Serial.println(SPIFFS.totalBytes(), HEX);
  Serial.println(SPIFFS.usedBytes(), HEX);

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
 
  while(file)
  {
    Serial.print("FILE: ");
    Serial.print(file.name());
    Serial.print("  ");
    Serial.println(file.size());
    //for (int j = 0; j < 10; j++) Serial.write(file.read());
    file.close();
    file = root.openNextFile();
  }
  
  /*
  File filez = SPIFFS.open("/index.htm");
  if(!filez){
    Serial.println("Failed to open file for reading");
    return;
  }
  
  Serial.println("File Content:");
  while(filez.available()){
    Serial.write(filez.read());
  }
  filez.close();
*/

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  server.addHandler(new SPIFFSEditor(SPIFFS, http_username,http_password));
  
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();

  Serial.println(WiFi.localIP());
}

uint32_t stamp = 0;
int cnt = 0;
char buffer[200];

/*
 * These below values set the various stuff you can currently set over websockets to the gevcu website.
 * It would set configuration options via a special interface that replaced ~Var~ instances with
 * actual values. But, the way the ESP32 works doesn't make that very simple. 
 * But, these below values are currently being sent over the websocket and are just json. So, 
 * it will be easy enough to have the TeensyMM side send json with parameters and get back json
 * with changes in order to operate. GEVCU7 is connected to the ESP32 over "Serial" at 115200
 * so use those settings at the other side too and just send us JSON for now. But, some
 * json will be for control of this system and not to update parameters on the website.
 * For one, we need to be able to set the wifi parameters:
 * esp32_ssid = ssid either to connect to or create
 * esp32_pw = Password for wifi either to connect to or create. If creating it'll be WPA2
 * esp32_wifimode = "CLIENT" if connecting to an SSID AP or "SERVER" if creating a softAP
 * 
 * 115200 gives us 11,520 characters per second. If the average json item is 27 bytes long
 * then that gives 426 updated items per second. There are 72 below so even running through
 * them all the serial link could do about 6 updates per second. Not all items will need
 * to update that frequently anyway so the load will be less.
 */

const String vals[] = {
"{\"systemState\": 8}",
"{\"timeRunning\": \"12:34:56\"}",
"{\"torqueActual\": 10.2}",
"{\"speedActual\": 45.2}",
"{\"throttle\": 23}",
"{\"dcVoltage\": 364.23}",
"{\"dcCurrent\": -23.43}",
"{\"acCurrent\": -10.42}",
"{\"temperatureMotor\": 30.43}",
"{\"temperatureController\": 43.22}",
"{\"mechanicalPower\": 234.00}",
"{\"bitfieldMotor\": 0}",
"{\"bitfieldBms\": 0}",
"{\"bitfieldIO\": 45}",
"{\"dcDcHvVoltage\": 345.00}",
"{\"dcDcLvVoltage\": 14.34}",
"{\"dcDcHvCurrent\": 1.00}",
"{\"dcDcLvCurrent\": 34.00}",
"{\"dcDcTemperature\": 52.32}",
"{\"chargerInputVoltage\": 245.33}",
"{\"chargerInputCurrent\": 13.53}",
"{\"chargerBatteryVoltage\": 363.34}",
"{\"chargerBatteryCurrent\": 12.34}",
"{\"chargerTemperature\": 23.34}",
"{\"chargerInputCurrentTarget\": 15.00}",
"{\"chargeHoursRemain\": 2.342}",
"{\"chargeMinsRemain\": 0.00}",
"{\"chargeLevel\": 52.34}",
"{\"flowCoolant\": 1.00}",
"{\"flowHeater\": 2.00}",
"{\"heaterPower\": 232.00}",
"{\"temperatureBattery1\": 21.0}", 
"{\"temperatureBattery2\": 23.0}", 
"{\"temperatureBattery3\": 25.0}", 
"{\"temperatureBattery4\": 27.0}",
"{\"temperatureBattery5\": 29.0}", 
"{\"temperatureBattery6\": 31.0}",
"{\"temperatureCoolant\": 34.00}",
"{\"temperatureHeater\": 12.00}",
"{\"temperatureExterior\": 10.32}",
"{\"powerSteering\": 1}",
"{\"enableRegen\": true}",
"{\"enableHeater\": false}",
"{\"enableCreep\": true}",
"{\"cruiseSpeed\": true}",
"{\"enableCruiseControl\": true}",
"{\"soc\": 62.43}",
"{\"dischargeLimit\": 100.42}",
"{\"chargeLimit\": 234.00}",
"{\"chargeAllowed\": 0}",
"{\"dischargeAllowed\": 1}",
"{\"lowestCellTemp\": -1.00}",
"{\"highestCellTemp\": 23.00}",
"{\"lowestCellVolts\": 3.24}",
"{\"highestCellVolts\": 4.1}",
"{\"averageCellVolts\": 3.6}",
"{\"deltaCellVolts\": 0.123}",
"{\"lowestCellResistance\": 0.001}",
"{\"highestCellResistance\": 0.02}",
"{\"averageCellResistance\": 0.004}",
"{\"deltaCellResistance\": 0.0044}",
"{\"lowestCellTempId\": 0}",
"{\"highestCellTempId\": 12}",
"{\"lowestCellVoltsId\": 42}",
"{\"highestCellVoltsId\": 23}",
"{\"lowestCellResistanceId\": 12}",
"{\"highestCellResistanceId\": 42}",
"{\"packResistance\": 0.0001}",
"{\"packHealth\": 84.52}",
"{\"packCycles\": 2345}",
"{\"bmsTemp\": 23.23}",
"\"limits\": {\"dcVoltage\": {\"min\": 200.0, \"max\": 400.0},\"dcCurrent\": {\"min\": 0.0, \"max\": 336.34},\"temperatureController\": {\"min\": 0, \"max\": 60.0},\"temperatureMotor\": {\"min\": 0, \"max\": 90.0} }",

"{\"minimumLevel\": 500}",
"{\"minimumLevel2\": 4000}",
"{\"maximumLevel\": 2500}",
"{\"maximumLevel2\": 2000}",
"{\"positionRegenMaximum\": 5}",
"{\"positionRegenMaximum\": 15}",
"{\"positionForwardStart\": 20}",
"{\"positionHalfPower\": 65}",
"{\"minimumRegen\": 5}",
"{\"maximumRegen\": 75}",
"{\"creepLevel\": 2}",
"{\"creepSpeed\": 400}",
"{\"brakeHold\": 10}",
"{\"brakeMinimumLevel\": 200}",
"{\"brakeMinimumRegen\": 10}",
"{\"brakeMaximumLevel\": 2000}",
"{\"brakeMaximumRegen\": 90}",
"{\"inputCurrent\": 10}",
};

char *getTimeRunning()
{
    uint32_t ms = millis();
    int seconds = (int) (ms / 1000) % 60;
    int minutes = (int) ((ms / (1000 * 60)) % 60);
    int hours = (int) ((ms / (1000 * 3600)) % 24);
    sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
    return buffer;
}

void loop(){
  ArduinoOTA.handle();
  ws.cleanupClients();
  if (millis() > (stamp + 40))
  {
      stamp = millis();
      //TODO: Here is where the above parameters are sent over and over to the webpage to update settings.
      //This should instead be getting values from GEVCU7 to update settings. 
      ws.printfAll(vals[cnt].c_str());
      cnt = (cnt + 1) % 90;

      //just to give feedback that we're doing something. But, it's kind of annoying too.
      //Serial.write('!');
      //if (cnt == 0) Serial.println();
  }
}

#include <ArduinoOTA.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_task_wdt.h"

//causes several lines of text to get sent to GEVCU7 to give more feedback about what is going on.
//#define EXTRA_DEBUG

//defining this causes the code to create a web server upon wifi connection
#define WEBINTERFACE

//and, defining this creates two telnet interfaces for the two serial ports on GEVCU7
#define TELNET

// SKETCH BEGIN

#ifdef WEBINTERFACE
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
#endif

#ifdef TELNET
WiFiServer telnetServer(23);
WiFiServer statusServer(2323);
WiFiClient telnetClient;
WiFiClient statusClient;
#endif
WiFiClient wifiClient;
WiFiUDP wifiUDPServer;
String lineBuffer;
#ifdef TELNET
char tcpBuffer[1500];
char telnetOutBuff[1200];
uint16_t telnetOutBuffLength;
uint32_t telnetOutLastWrite;
#endif

int OTAcount = 0;
char ssid[80];
char password[80];
char hostName[80];
uint8_t wifiMode; //0 = create a mini-AP, 1 = connect to an AP
uint8_t softAPActive = false;

uint32_t stamp = 0;
int cnt = 0;
uint32_t lastBroadcast;
char buffer[200];

#ifdef TELNET
char telnetInputBuff[1000];
char statusInputBuff[100];
uint16_t telnetInputBuffIdx = 0;
uint16_t statusInputBuffIdx = 0;
#endif

const char* http_username = "admin";
const char* http_password = "admin";

static IPAddress broadcastAddr(255,255,255,255);

bool loadTeensyFile(String filename);
void execOTA(int type);

#ifdef TELNET
void sendTelnetLine(const char *line)
{
    if (telnetClient && telnetClient.connected())
    {
        telnetClient.println(line);
    }
}
#endif

void printSerialAndTelnet(String line)
{
    printf("%s", line.c_str());
#ifdef TELNET
    if (telnetClient && telnetClient.connected())
    {
        telnetClient.println(line);
    }
#endif
}

#ifdef TELNET
void printStatusToNetwork(String line)
{
    if (statusClient && statusClient.connected())
    {
        statusClient.println(line);
    }
}
#endif

void printlnSerialAndTelnet(String line)
{
    printf("%s\n", line.c_str());
#ifdef TELNET
    if (telnetClient && telnetClient.connected())
    {
        telnetClient.println(line);
    }
#endif
}

#ifdef TELNET
void sendTelnetBytes(const char *data, int length)
{
    if (telnetClient && telnetClient.connected())
    {
        telnetClient.write(data, length);
    }
}
#endif

#ifdef WEBINTERFACE
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
    if(type == WS_EVT_CONNECT)
    {
        Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
        //client->printf("Hello Client %u :)", client->id());
        //client->ping();
    }
    else if(type == WS_EVT_DISCONNECT)
    {
        Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
    } 
    else if(type == WS_EVT_ERROR)
    {
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    } 
    //else if(type == WS_EVT_PONG)
    //{
    //    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
    //    client->text("PONG BRO!");
    //}
    else if(type == WS_EVT_DATA)
    {
        /*
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        String msg = "";
        if(info->final && info->index == 0 && info->len == len)
        {
            //the whole message is in a single frame and we got all of it's data
            Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

            if(info->opcode == WS_TEXT)
            {
                for(size_t i=0; i < info->len; i++)
                {
                    msg += (char) data[i];
                }
            }
            else
            {
                char buff[3];
                for(size_t i=0; i < info->len; i++)
                {
                    sprintf(buff, "%02x ", (uint8_t) data[i]);
                    msg += buff ;
                }
            }
            Serial.printf("%s\n",msg.c_str());

            if(info->opcode == WS_TEXT)
                client->text("I got your text message");
            else
                client->binary("I got your binary message");
        }
        else
        {
            //message is comprised of multiple frames or the frame is split into multiple packets
            if(info->index == 0)
            {
                if(info->num == 0)
                    Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
                Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
            }

            Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

            if(info->opcode == WS_TEXT)
            {
                for(size_t i=0; i < len; i++)
                {
                    msg += (char) data[i];
                }
            }
            else
            {
                char buff[3];
                for(size_t i=0; i < len; i++)
                {
                    sprintf(buff, "%02x ", (uint8_t) data[i]);
                    msg += buff ;
                }
            }
            Serial.printf("%s\n",msg.c_str());

            if((info->index + len) == info->len)
            {
                Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
                if(info->final)
                {
                    Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
                    if(info->message_opcode == WS_TEXT)
                        client->text("I got your text message");
                    else
                        client->binary("I got your binary message");
                }
            }
        }*/

    }
}
#endif

void attemptConnection()
{
    if (wifiMode == 0)
    {
        WiFi.mode(WIFI_AP_STA);
        if (strlen(password) > 0) WiFi.softAP(ssid, password);
        else WiFi.softAP(ssid); //open AP
        softAPActive = true;
    }
    else
    {        
        WiFi.begin(ssid, password);
        if (WiFi.waitForConnectResult() != WL_CONNECTED)
        {
            Serial.printf("STA: Failed!\n");
            WiFi.disconnect(false);
            delay(1000);
            WiFi.begin(ssid, password);
        }
        if (WiFi.waitForConnectResult() != WL_CONNECTED) return; //don't do the rest if we could not connect.
        WiFi.setAutoReconnect(true); //if we did connect then try to reconnect on failure
    }

#ifdef WEBINTERFACE
    //Send OTA events to the browser
    ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
    ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        char p[32];
        sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
        events.send(p, "ota");
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
        else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
        else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
        else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
        else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
    });
#endif

    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.begin();

    MDNS.begin(hostName);

#ifdef TELNET
    telnetServer.end();
    telnetServer.begin(23);
    telnetServer.setNoDelay(true);

    statusServer.end();
    statusServer.begin(2323);
    statusServer.setNoDelay(true);

    MDNS.addService("telnet","tcp", 23);
    MDNS.addService("status","tcp", 2323);
#endif

#ifdef WEBINTERFACE
    MDNS.addService("http","tcp",80);
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
  
    server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.onNotFound([](AsyncWebServerRequest *request)
    {
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

        if(request->contentLength())
        {
            Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
            Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
        }

        int headers = request->headers();
        int i;
        for(i=0;i<headers;i++)
        {
            AsyncWebHeader* h = (AsyncWebHeader *)request->getHeader(i);
            Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
        }

        int params = request->params();
        for(i=0;i<params;i++)
        {
            AsyncWebParameter* p = (AsyncWebParameter *)request->getParam(i);
            if(p->isFile())
            {
                Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
            } 
            else if(p->isPost())
            {
                Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
            } 
            else 
            {
                Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
            }
        }
        request->send(404);
    });

    server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
    {
        if(!index)
            Serial.printf("UploadStart: %s\n", filename.c_str());
        Serial.printf("%s", (const char*)data);
        if(final)
            Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    {
        if(!index)
            Serial.printf("BodyStart: %u\n", total);
        Serial.printf("%s", (const char*)data);
        if(index + len == total)
            Serial.printf("BodyEnd: %u\n", total);
    });
    server.begin();
#endif

    if (wifiMode == 1) Serial.println(WiFi.localIP());
    if (wifiMode == 0) Serial.println(WiFi.softAPIP());
}

void setup()
{
    Serial.setRxBufferSize(1024);
    Serial.setTxBufferSize(1024);
    Serial.begin(230400);
    delay(2000);
    //Serial.setDebugOutput(true);

    ssid[0] = 0;
    password[0] = 0;
    hostName[0] = 0;
    wifiMode = 0;
    lastBroadcast = 0;
#ifdef TELNET
    telnetOutBuffLength = 0;
    telnetOutLastWrite = millis();
    telnetInputBuffIdx = 0;
    statusInputBuffIdx = 0;
    memset(telnetInputBuff, 0, 1000);
#endif
    //attemptConnection();

    Serial.println("About to start LittleFS");

    if(!LittleFS.begin(false))
    {
        Serial.println("LittleFS Mount Failed");
    }
    //Serial.println(SPIFFS.totalBytes(), HEX);
    //Serial.println(SPIFFS.usedBytes(), HEX);

/*
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
  */

    Serial.println("BOOTOK");
}

char *getTimeRunning()
{
    uint32_t ms = millis();
    int seconds = (int) (ms / 1000) % 60;
    int minutes = (int) ((ms / (1000 * 60)) % 60);
    int hours = (int) ((ms / (1000 * 3600)) % 24);
    sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
    return buffer;
}

void loop()
{
    ArduinoOTA.handle();

#ifdef WEBINTERFACE    
    ws.cleanupClients();
#endif
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n')
        {
            //handle input here
            if (lineBuffer[0] == '{')
            {
                StaticJsonDocument<5000>doc;
                DeserializationError err = deserializeJson(doc, lineBuffer.c_str());
                if (err)
                {
                    Serial.print(F("deserializeJson() failed with code "));
                    Serial.println(err.f_str());
                }
                else
                {
                    JsonObject obj = doc.as<JsonObject>();
                    if (obj.containsKey("SSID"))
                    {
                        strlcpy(ssid, doc["SSID"] | "GEVCU7", 64);
                        strlcpy(password, doc["WIFIPW"] | "Default123", 64);
                        strlcpy(hostName, doc["HostName"] | "gevcu7", 64);
                        wifiMode = doc["WiFiMode"];
                        attemptConnection();
                    }
                    else if (obj.containsKey("FWUPD"))
                    {
                        String filename = doc["FWUPD"];
                        if (filename.length() < 2) filename = "GEVCU7";
                        loadTeensyFile(filename);
                    }
                    else if (obj.containsKey("ESPUPD"))
                    {
                        int updateType = doc["ESPUPD"];                        
                        execOTA(updateType);
                    }
                }
            }
            if (lineBuffer[0] == '~')
            {
                //lineBuffer += "\n"; //make sure to send the line ending too.
                lineBuffer[0] = ' ';
#ifdef TELNET
                telnetOutBuffLength += sprintf(&telnetOutBuff[telnetOutBuffLength], "%s\n", lineBuffer.c_str());
                //Serial.println(telnetOutBuffLength);
                //sendTelnetLine(lineBuffer.c_str());
#endif
            }

            if (lineBuffer[0] == '`')
            {
                //lineBuffer += "\n"; //make sure to send the line ending too.
                lineBuffer[0] = ' ';
#ifdef TELNET
                //telnetOutBuffLength += sprintf(&telnetOutBuff[telnetOutBuffLength], "%s\n", lineBuffer.c_str());
                //Serial.println(telnetOutBuffLength);
                printStatusToNetwork(lineBuffer);
#endif
            }

            lineBuffer = "";
        }
        else lineBuffer += c;
    }
#ifdef TELNET    
    //if telnet buffer is getting full or 300ms has gone by then flush the buffer
    if ( 
        (telnetOutBuffLength > 1000) 
     || ( ((millis() - 300) > telnetOutLastWrite)  && (telnetOutBuffLength > 0) ) 
       )
    {
        sendTelnetBytes(telnetOutBuff, telnetOutBuffLength);
        telnetOutBuffLength = 0;
        telnetOutLastWrite = millis();
    } 
#endif

    if ( (WiFi.status() == WL_CONNECTED) || (softAPActive == true) )
    {
        if ((micros() - lastBroadcast) > 1000000ul) //every second send out a broadcast ping
        {
            lastBroadcast = micros();
            wifiUDPServer.beginPacket(broadcastAddr, 17222);
            wifiUDPServer.write((uint8_t *)hostName, strlen(hostName));
            wifiUDPServer.endPacket();
        }

#ifdef TELNET
        if (telnetServer.hasClient())
        {
            telnetClient = telnetServer.available();
            delay(100);
            //grab any extra crap that comes in at the start of the connection and throw it away.
            while (telnetClient.available())
            {
                int retBytes = telnetClient.read((uint8_t *)tcpBuffer, 1500);
            }
#ifdef EXTRA_DEBUG
            Serial.println("Got a new telnet client");
#endif
        }

        if (statusServer.hasClient())
        {
            statusClient = statusServer.available();
            delay(100);
            //grab any extra crap that comes in at the start of the connection and throw it away.
            while (statusClient.available())
            {
                int retBytes = statusClient.read((uint8_t *)tcpBuffer, 1500);
            }
#ifdef EXTRA_DEBUG
            Serial.println("Got a new status client");
#endif
        }

        if (telnetClient && telnetClient.connected())
        {
            while (telnetClient.available() > 0)
            {
                int retBytes = telnetClient.read((uint8_t *)tcpBuffer, 1500);
#ifdef EXTRA_DEBUG
                Serial.printf("Got %i from telnet client\n", retBytes);
#endif                
                for (int i = 0; i < retBytes; i++)
                {
                    telnetInputBuff[telnetInputBuffIdx++] = tcpBuffer[i];
                    if (tcpBuffer[i] == '\n') //dump buffer at line breaks
                    {

                        Serial.write('~');
                        Serial.write(telnetInputBuff, telnetInputBuffIdx);
                        telnetInputBuffIdx = 0;
                    }
                }
            }
        }

        if (statusClient && statusClient.connected())
        {
            while (statusClient.available() > 0)
            {
                int retBytes = statusClient.read((uint8_t *)tcpBuffer, 1500);
#ifdef EXTRA_DEBUG
                Serial.printf("Got %i from status client\n", retBytes);
#endif
                
                for (int i = 0; i < retBytes; i++)
                {
                    statusInputBuff[statusInputBuffIdx++] = tcpBuffer[i];
                    if (tcpBuffer[i] == '\n') //dump buffer at line breaks
                    {
                        Serial.write('`');
                        Serial.write(statusInputBuff, statusInputBuffIdx);
                        statusInputBuffIdx = 0;
                    }
                }
            }
        }
#endif
    }
}

// Utility to extract header value from headers
String getHeaderValue(String header, String headerName)
{
    return header.substring(strlen(headerName.c_str()));
}

void onOTAProgress(uint32_t progress, size_t fullSize)
{

    //esp_task_wdt_reset();
    // printf("%u of %u bytes written to memory...\n", progress, fullSize);


    if (OTAcount++ == 10)
    {
        printf("..%u\n", progress);
        OTAcount = 0;
    }
    else printf("..%u", progress);
}

//see the below loadOTAFile version for the comments about how this all works
bool loadTeensyFile(String filename)
{
    int contentLength = 0;
    bool isValidContentType = false;

    String host = "firmware.evtv.me";
    filename = "/" + filename + ".hex";

    if (wifiClient.connect(host.c_str(), 80))
    {
        wifiClient.print(String("GET ") + filename + " HTTP/1.1\r\n" +
                         "Host: " + host + "\r\n" +
                         "Cache-Control: no-cache\r\n" +
                         "Connection: close\r\n\r\n");  // Get the contents of the bin file

        unsigned long timeout = millis();
        while (wifiClient.available() == 0)
        {
            if ( (millis() - timeout) > 5000 )
            {
                printlnSerialAndTelnet("Client Timeout !");
                wifiClient.stop();
                return false;
            }
        }
        while (wifiClient.available())
        {
            String line = wifiClient.readStringUntil('\n');// read line till /n
            line.trim();// remove space, to check if the line is end of headers

            if (!line.length()) {
                break;
            }

            // Check if the HTTP Response is 200 else break and Exit Update
            if (line.startsWith("HTTP/1.1"))
            {
                if (line.indexOf("200") < 0)
                {
                    printlnSerialAndTelnet("FAIL...Got a non 200 status code from server. Exiting OTA Update.");
                    break;
                }
            }

            if (line.startsWith("Content-Length: "))
            {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                printlnSerialAndTelnet("              ...Server indicates " + String(contentLength) + " byte file size\n");
            }

            if (line.startsWith("Content-Type: "))
            {
                String contentType = getHeaderValue(line, "Content-Type: ");
                printlnSerialAndTelnet("\n              ...Server indicates correct " + contentType + " payload.\n");
                if (contentType == "binary/octet-stream")
                {
                    isValidContentType = true;
                }
            }
        } //end while client available
    }
    else {
        // Connection failed
        printlnSerialAndTelnet("Connection to " + String(host) + " failed. Please check your setup");
    }

    //Serial.println("File length: " + String(contentLength) + ", Valid Content Type flag:" + String(isValidContentType));

    // check contentLength and content type
    if (contentLength && isValidContentType) // Check if there is enough to OTA Update
    {
        printlnSerialAndTelnet("Everything checks out. Starting hex stream");
        Serial.write((char)0xA5); //signals to teensy that we're about to send an update
        delay(250); //wait a little bit for the teensy side to be ready to receive
        while (1) //no idea how we'd stop this. But, teensy is likely to reboot if it all worked
        {
            //printSerialAndTelnet("We have space for the update...starting transfer... \n\n");
            // delay(1100);
            //size_t written = Update.writeStream(wifiClient);
            String line = wifiClient.readStringUntil('\n');// read line till /n
            line.trim();// remove space, to check if the line is end of headers
            if (line.length() > 2) Serial.println(line); //don't send any blank lines in case they appear
            delay(2); //about enough time to send a whole line
        } //end if can begin
    } //End contentLength && isValidContentType
    else
    {
        //printlnSerialAndTelnet("There was no content in the response");
        wifiClient.flush();
        return false;
    }
    return true;
}

bool loadOTAFile(String filename, bool isMainFirmware)
{
    int contentLength = 0;
    bool isValidContentType = false;
    String host = "firmware.evtv.me";

    //printlnSerialAndTelnet("Connecting to AWS server: " + String(host)); // Connect to S3

    if (wifiClient.connect(host.c_str(), 80))
    {
        //Serial.println("Searching for: " + String(bin)); //Connection succeeded -fetching the binary
        wifiClient.print(String("GET ") + filename + " HTTP/1.1\r\n" +
                         "Host: " + host + "\r\n" +
                         "Cache-Control: no-cache\r\n" +
                         "Connection: close\r\n\r\n");  // Get the contents of the bin file

        unsigned long timeout = millis();
        while (wifiClient.available() == 0)
        {
            if ( (millis() - timeout) > 5000 )
            {
                //printlnSerialAndTelnet("Client Timeout !");
                wifiClient.stop();
                return false;
            }
        }
        // Once the response is available,
        // check stuff

        /*
           Response Structure
            HTTP/1.1 200 OK
            x-amz-id-2: NVKxnU1aIQMmpGKhSwpCBh8y2JPbak18QLIfE+OiUDOos+7UftZKjtCFqrwsGOZRN5Zee0jpTd0=
            x-amz-request-id: 2D56B47560B764EC
            Date: Wed, 14 Jun 2017 03:33:59 GMT
            Last-Modified: Fri, 02 Jun 2017 14:50:11 GMT
            ETag: "d2afebbaaebc38cd669ce36727152af9"
            Accept-Ranges: bytes
            Content-Type: application/octet-stream
            Content-Length: 357280
            Server: AmazonS3

            {{BIN FILE CONTENTS}}

        */
        while (wifiClient.available())
        {
            String line = wifiClient.readStringUntil('\n');// read line till /n
            line.trim();// remove space, to check if the line is end of headers
            //printlnSerialAndTelnet(line);

            // if the the line is empty,this is end of headers break the while and feed the
            // remaining `client` to the Update.writeStream();

            if (!line.length()) {
                break;
            }


            // Check if the HTTP Response is 200 else break and Exit Update

            if (line.startsWith("HTTP/1.1"))
            {
                if (line.indexOf("200") < 0)
                {
                    //printlnSerialAndTelnet("FAIL...Got a non 200 status code from server. Exiting OTA Update.");
                    break;
                }
            }

            // extract headers here
            // Start with content length

            if (line.startsWith("Content-Length: "))
            {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                //printlnSerialAndTelnet("              ...Server indicates " + String(contentLength) + " byte file size\n");
            }

            if (line.startsWith("Content-Type: "))
            {
                String contentType = getHeaderValue(line, "Content-Type: ");
                //printlnSerialAndTelnet("\n              ...Server indicates correct " + contentType + " payload.\n");
                if (contentType == "application/octet-stream")
                {
                    isValidContentType = true;
                }
            }
        } //end while client available
    }
    else {
        // Connect to S3 failed
        //printlnSerialAndTelnet("Connection to " + String(host) + " failed. Please check your setup");
    }

    // Check what is the contentLength and if content type is `application/octet-stream`
    //Serial.println("File length: " + String(contentLength) + ", Valid Content Type flag:" + String(isValidContentType));

    // check contentLength and content type
    if (contentLength && isValidContentType) // Check if there is enough to OTA Update
    {
        bool canBegin = Update.begin(contentLength, isMainFirmware ? U_FLASH : U_SPIFFS);
        if (canBegin)
        {
            //printSerialAndTelnet("We have space for the update...starting transfer... \n\n");
            // delay(1100);
            size_t written = Update.writeStream(wifiClient);

            if (written == contentLength)
            {
                //printlnSerialAndTelnet("\nWrote " + String(written) + " bytes to memory...");
                yield();
            }
            else
            {
                //printlnSerialAndTelnet("\n********** FAILED - Wrote:" + String(written) + " of " + String(contentLength) + ". We'll try again...********\n\n" );
                //execOTA(type);
            }

            if (Update.end())
            {
                //  Serial.println("OTA file transfer completed!");
                if (Update.isFinished())
                {
                    if (isMainFirmware)
                    {
                        //printlnSerialAndTelnet("Rebooting new firmware...\n");
                        yield();
                        delay(1000);
                        ESP.restart();
                    }
                    //else
                    //{
                    //    printlnSerialAndTelnet("Applied new webpage image sucessfully!\n");
                   // }
                }
                //else printlnSerialAndTelnet("FAILED...update not finished? Something went wrong!");

            }
            else
            {
                //printlnSerialAndTelnet("Error Occurred. Error #: " + String(Update.getError()));
                //execOTA(type);
            }
        } //end if can begin
        else {
            // not enough space to begin OTA
            // Understand the partitions and space availability

            //printlnSerialAndTelnet("Not enough space to begin OTA");
            wifiClient.flush();
        }
    } //End contentLength && isValidContentType
    else
    {
        //printlnSerialAndTelnet("There was no content in the response");
        wifiClient.flush();
        return false;
    }
    return true;

}

void execOTA(int type)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        //Serial.printf("\n\n\n******* WIFI STATUS..connected to local Access Point: %s as IP: %s signal strength: %idBm ******\n\n",
        //                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        //printSerialAndTelnet("We have a wireless connection...Initiating Wireless Over-the-Air Firmware Update...\n\n");
    }
    else
    {
        //Serial.print("\n\nYou must have a wireless connection to do OTA firmware update...\n\n");
        //delay(2000);
        return;
    }

    String bin;
    //all filenames should have a slash to start

    esp_task_wdt_reset();        //Feed our watchdog
    Update.onProgress(onOTAProgress);

    if (type == 1234) bin = "/GEVCU7_ESP32_WebPage.test.bin"; // test webpages for special people
    else if (type == 1337) bin = "/GEVCU7_ESP32_WebPage.debug.bin"; // test webpages probably just for Collin
    else if (type == 265)
    {
        bin = "/GEVCU7_ESP32_WebPage.debug.bin";
        LittleFS.end(); //close it so we can reopen it and hopefully have the updated copy once we're done loading
    }
    else bin = "/GEVCU7_ESP32_WebPage.bin"; // normal webpage file
    loadOTAFile(bin, false); //spiffs file for webpage
    if (type == 265)  //in this case, do not do a firmware upgrade.
    {
        LittleFS.begin();
        return;
    }

    if (type == 1234) bin = "/GEVCU7_ESP32_WebHost.test.bin"; // test firmware for special people
    else if (type == 1337) bin = "/GEVCU7_ESP32_WebHost.debug.bin"; // test firmware probably just for Collin
    else bin = "/GEVCU7_ESP32_WebHost.bin"; // normal firmware file
    loadOTAFile(bin, true);

} //End execOTA()

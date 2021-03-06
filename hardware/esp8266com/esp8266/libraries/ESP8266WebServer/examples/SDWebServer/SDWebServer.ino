/* 
  SDWebServer - Example WebServer with SD Card backend for esp8266

  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.
 
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Have a FAT Formatted SD Card connected to the SPI port of the ESP8266
  The web root is the SD Card root folder
  File extensions with more than 3 charecters are not supported by the SD Library
  File Names longer than 8 charecters will be truncated by the SD library, so keep filenames shorter
  index.htm is the default index (works on subfolders as well)
  
  upload the contents of SdRoot to the root of the SDcard and access the editor by going to http://esp8266sd.local/edit

*/
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>
#include <SD.h>

#define WWW_BUF_SIZE 1460
#define DBG_OUTPUT_PORT Serial

const char* ssid = "**********";
const char* password = "**********";
const char* hostname = "esp8266sd";

MDNSResponder mdns;
ESP8266WebServer server(80);

static bool hasSD = false;
File uploadFile;

void returnOK(){
  WiFiClient client = server.client();
  String message = "HTTP/1.1 200 OK\r\n";
  message += "Content-Type: text/plain\r\n";
  message += "Connection: close\r\n";
  message += "Access-Control-Allow-Origin: *\r\n";
  message += "\r\n";
  client.print(message);
  message = 0;
  client.stop();
}

void returnFail(String msg){
  WiFiClient client = server.client();
  String message = "HTTP/1.1 500 Fail\r\n";
  message += "Content-Type: text/plain\r\n";
  message += "Connection: close\r\n";
  message += "Access-Control-Allow-Origin: *\r\n";
  message += "\r\n";
  message += msg;
  message += "\r\n";
  client.print(message);
  message = 0;
  client.stop();
}

bool loadFromSdCard(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";
  
  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";
  
  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()){
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }
  
  if(server.hasArg("download")) dataType = "application/octet-stream";
  
  if (dataFile) {
    WiFiClient client = server.client();
    String head = "HTTP/1.1 200 OK\r\nContent-Type: ";
    head += dataType;
    head += "\r\nContent-Length: ";
    head += dataFile.size();
    head += "\r\nConnection: close";
    head += "\r\nAccess-Control-Allow-Origin: *";
    head += "\r\n\r\n";
    client.print(head);
    dataType = 0;
    path = 0;
    
    uint8_t obuf[WWW_BUF_SIZE];
    
    while (dataFile.available() > WWW_BUF_SIZE){
      dataFile.read(obuf, WWW_BUF_SIZE);
      if(client.write(obuf, WWW_BUF_SIZE) != WWW_BUF_SIZE){
        DBG_OUTPUT_PORT.println("Sent less data than expected!");
        dataFile.close();
        return true;
      }
    }
    uint16_t leftLen = dataFile.available();
    dataFile.read(obuf, leftLen);
    if(client.write(obuf, leftLen) != leftLen){
      DBG_OUTPUT_PORT.println("Sent less data than expected!");
      dataFile.close();
      return true;
    }
    dataFile.close();
    client.stop();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    if(SD.exists((char *)upload.filename.c_str())) SD.remove((char *)upload.filename.c_str());
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(upload.filename);
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(uploadFile) uploadFile.write(upload.buf, upload.buflen);
    DBG_OUTPUT_PORT.print("Upload: WRITE, Bytes: "); DBG_OUTPUT_PORT.println(upload.buflen);
  } else if(upload.status == UPLOAD_FILE_END){
    if(uploadFile) uploadFile.close();
    DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(upload.size);
  }
}

void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }
  file.rewindDirectory();
  File entry;
  String entryPath;
  while(true) {
    entry = file.openNextFile();
    if (!entry) break;
    entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    entryPath = 0;
    yield();
  }
  SD.rmdir((char *)path.c_str());
  path = 0;
  file.close();
}

void handleDelete(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  deleteRecursive(path);
  returnOK();
  path = 0;
}

void handleCreate(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
  path = 0;
}

void printDirectory() {
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String path = server.arg("dir");
  if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  File dir = SD.open((char *)path.c_str());
  path = 0;
  if(!dir.isDirectory()){
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  
  File entry;
  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/json\r\n\r\n");
  String output = "[";
  while(true) {
   entry = dir.openNextFile();
   if (!entry) break;
   if(output != "[") output += ',';
   output += "{\"type\":\"";
   output += (entry.isDirectory())?"dir":"file";
   output += "\",\"name\":\"";
   output += entry.name();
   output += "\"";
   output += "}";
   entry.close();
   if(output.length() > 1460){
     client.write(output.substring(0, 1460).c_str(), 1460);
     output = output.substring(1460);
   }
 }
 dir.close();
 output += "]";
 client.write(output.c_str(), output.length());
 client.stop();
 output = 0;
}

void handleNotFound(){
  if(hasSD && loadFromSdCard(server.uri())) return;
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  DBG_OUTPUT_PORT.print(message);
}

void setup(void){
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");
  WiFi.begin(ssid, password);
  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);

  // Wait for connection
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
  }
  if(i == 21){
    DBG_OUTPUT_PORT.print("Could not connect to");
    DBG_OUTPUT_PORT.println(ssid);
    while(1) delay(500);
  }
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());
  /*
  if (mdns.begin(hostname, WiFi.localIP())) {
    DBG_OUTPUT_PORT.println("MDNS responder started");
    DBG_OUTPUT_PORT.print("You can now connect to http://");
    DBG_OUTPUT_PORT.print(hostname);
    DBG_OUTPUT_PORT.println(".local");
  }
  */
  
  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, [](){ returnOK(); });
  server.onNotFound(handleNotFound);
  server.onFileUpload(handleFileUpload);
  
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");
  
  if (SD.begin(SS)){
     DBG_OUTPUT_PORT.println("SD Card initialized.");
     hasSD = true;
  }
}
 
void loop(void){
  server.handleClient();
}

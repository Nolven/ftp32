#include <WiFi.h>

#define FTP32_LOG FTP32_LOG_INFO
#include "ftp32.h"

void setup(){
    Serial.begin(115200);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin("wifi", "ssid");

    while( WiFi.status() != WL_CONNECTED ){
      delay(100);
    }

    FTP32 ftp("192.168.1.1", 21);

    if( ftp.connectWithPassword("test", "test") ){
        Serial.println("Login unsuccessful");
        Serial.printf("Exited with code: %d %s\n", ftp.getLastCode(), ftp.getLastMsg());
        while(true){}
    }

    if( ftp.uploadSingleshot("/test.file", "msg to santa", FTP32::OpenType::CREATE_REPLACE) ){
        Serial.println("Upload failed");
        Serial.printf("Exited with code: %d %s", ftp.getLastCode(), ftp.getLastMsg());    
    }

    String downloadedData;
    if( ftp.downloadSingleshot("/test.file", downloadedData) ){
        Serial.println("Upload failed");
        Serial.printf("Exited with code: %d %s", ftp.getLastCode(), ftp.getLastMsg());    
    } else {
      Serial.println(downloadedData);
    }
}

void loop(){}
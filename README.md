![Version Badge](https://img.shields.io/badge/version-1.0.0-cyan)


# FTP client for esp32
Header-only FTP client library.
Initially developed for video/pic transmission from esp32-cam.  
Inspired by ldabs [ESP32_FTPClient](https://github.com/ldab/ESP32_FTPClient)

# Main ideas
* Controllable logging (FATAL, ERROR and INFO levels);
* Success of each operation can be monitored through return code;
* Documented and simplified for those unfamiliar with FTP;
* Why header only? I don't remember

# Getting started
```c++
  // by default the library will stay silent
  // but if you want to have some output
  // set FTP32_LOG to FTP32_LOG_[...] [INFO], [ERROR], [FATAL] 
  // (it should be set before #include)
  #define FTP32_LOG FTP32_LOG_ERROR
  #include "FTP32/ftp32.h"

  // assume WiFiClient already started

  // create object and connect to your server
  FTP32 ftp{"127.0.0.1", 21};

  // if something goes wrong most methods return error code
  // or 0 if everything's fine
  if( ftp.connectWithPassword("user", "pass") ) 
  { 
    // you can check what happened and react accordingly 
    Serial.println(ftp.getLastMsg());
    Serial.println(ftp.getLastCode());
    return;
  }

  String content;

  // =========== now you can download files from server

  // =========== in a single transaction
  if( ftp.downloadSingleshot("/test.txt", content) ){
    Serial.println(ftp.getLastMsg());
    Serial.println(ftp.getLastCode());
  } else {
    Serial.println(content);
  }

  // =========== or in multiple batches
  
  // get file size
  size_t fSize;
  if( ftp.fileSize("/test.txt", fSize) )
  {
    Serial.println(ftp.getLastMsg());
    Serial.println(ftp.getLastCode()); 
  }

  // load the file
  char data[fSize];
  if( !ftp.initDownload("/test.txt") ){
    char data[fSize];
    char* it = data;
    size_t read{};
    // you can specify "the number of bytes you want to load at a time
    // downloadData returns number of bytes read; 0 means the end of download
    while( (read = ftp.downloadData(it, 5)) ){ it += read; }
  }

  // by this point, the entire file is loaded
```
# Contributing 
* If you want something implemented, open new issue ticket
* If you want to expand the lib, before and after adding new functionality execute `teset_all(...)` function and update it according to changes.

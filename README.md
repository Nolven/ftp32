![Version Badge](https://img.shields.io/badge/version-0.8.0_(MVP)-blue)


# FTP client for esp32
Header-only FTP client library.
Initially developed for video/pic transmission from esp32-cam.  
Inspired by ldabs [ESP32_FTPClient](https://github.com/ldab/ESP32_FTPClient)

## Main ideas
* No serial. Library won't print a thing
* Success of each operation can be monitored through return code 
* Documented and simplified for those unfamiliar with FTP

## Getting started
```c++
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

### Roadmap
#### Open a new issue if you want something to be implemented
#### Some points can be found in header file
* QoL improvments
* Arduino listing
* Examples (test function to check all methods)
* SSL layer

### Dependencies
* arduino-esp32
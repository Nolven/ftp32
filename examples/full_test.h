#include <exception>
#include <Arduino.h>

#define FTP32_LOG FTP32_LOG_INFO
#include "ftp32.h"

// you can this function to check 
// that all methods work correctly for your server
// if serial is not empty, something gone wrong
void test_all(const char* ip, uint8_t port, const char* username, const char* password){
    Serial.println("FTP32 test start");

    FTP32 ftp(ip, port);

    ftp.setControlChannelTimeout(10000);
    ftp.setDataChannelTimeout(20000);
    ftp.setMaxInBufferSize(80); // default size is 60

    // CONNECTION
    ftp.connectWithPassword(username, password);
    // sometime this thing prints an error (probably WiFiClient implemantation related)
    ftp.disconnect(); 
    ftp.connectWithPassword(username, password);

    // UPLOAD
    String data_p1 = "some";
    String data_p2 = " ";
    String data_p3 = "data";

    ftp.initUpload("/upload.multi", FTP32::CREATE_REPLACE);
    ftp.uploadData(data_p1.c_str());
    ftp.uploadData(data_p2.c_str());
    ftp.uploadData(data_p3.c_str());
    ftp.finishUpload();

    ftp.uploadSingleshot("/upload.single", String(data_p1 + data_p2 + data_p3).c_str(), FTP32::CREATE_REPLACE);

    // FILE UTILS
    ftp.renameFile("/upload.single", "/upload_single.renamed");
    ftp.deleteFile("/upload_single.renamed");

    size_t fsize;
    ftp.fileSize("/upload.multi", fsize);

    // DOWNLOAD
    String content;
    ftp.downloadSingleshot("/upload.multi", content);
    if( content != (data_p1 + data_p2 + data_p3) ){ 
        Serial.printf("Up|Down differs %s | %s\n", (data_p1 + data_p2 + data_p3).c_str(), content.c_str()); 
    }

    size_t read{};
    char raw_content[fsize];
    char* p = raw_content;
    ftp.initDownload("/upload.multi");

    // DIR
    ftp.mkdir("DIR");
    ftp.changeDir("DIR");
    ftp.pwd(content);
    ftp.rmdir("/DIR");
    ftp.mktree("/a/b/c/d/e/f/"); 
    ftp.rmtree("/a");

    // UTILS
    ftp.listContent("/", FTP32::ListType::HUMAN, content);
    ftp.listContent("/", FTP32::ListType::MACHINE, content);
    ftp.listContent("/", FTP32::ListType::SIMPLE, content);
    ftp.setTransferType(FTP32::TransferType::ASCII);
    ftp.setTransferType(FTP32::TransferType::BINARY);
    ftp.getLastModificationDate("/upload.multi", content);
    ftp.getSystemInfo(content);
}
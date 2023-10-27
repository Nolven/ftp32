#ifndef FTP32_H
#define FTP32_H

// the idea is to log_info the start of each operation
// and log_error only inside of _sendCmd method

// #define FTP32_LOG LOG_LEVEL_INFO             // TO SET DESIRED LOG_LVL
#define FTP32_LOG_FATAL 1 ///< if work continuation is impossible (mostly when control channel is disconnected)
#define FTP32_LOG_ERROR 2 ///< all cases when expected response code doesn't match
#define FTP32_LOG_INFO 3

#ifdef FTP32_LOG
#define FTP32_FATAL(...) if (FTP32_LOG >= FTP32_LOG_FATAL) { Serial.printf("[FTP32::FATAL] "); Serial.printf(__VA_ARGS__); Serial.println();} 
#define FTP32_ERROR(...) if (FTP32_LOG >= FTP32_LOG_ERROR) { Serial.printf("[FTP32::ERROR] "); Serial.printf(__VA_ARGS__); Serial.println();} 
#define FTP32_INFO(...)  if (FTP32_LOG >= FTP32_LOG_INFO) { Serial.printf("[FTP32::INFO] "); Serial.printf(__VA_ARGS__); Serial.println();} 
#else
#define FTP32_FATAL(...)
#define FTP32_ERROR(...)
#define FTP32_INFO(...)
#endif


#include <WiFiClient.h>
#include <stack>
#include <esp_timer.h>

/** @name Abbreviations
  * - CWD Current Working Dir
  * - DOSI Depends On Server Implementation
  **/

/** @name CommonReturnValues
  * @brief Most methods return the values from below if not stated otherwise.
  * 
  * - 0 on success
  * - FTP code otherwise (from 100 to 599)
  * - @see Error
  **/

class FTP32{
public:
  enum TransferType {BINARY, ASCII};
  enum OpenType {CREATE_REPLACE, APPEND};

  /** @enum ListType
    * Enumerates different types of directory listing formats for FTP commands.
    * Examples:
    * - HUMAN: -rw-r--r-- 1 user group 12345 Oct 15 09:30 file.txt
    * - MACHINE: Type=file;Size=12345;Modify=20231015093000; file.txt
    * - SIMPLE: file.txt
    **/
  enum ListType {
      HUMAN,    ///< Human-readable directory listing.
      MACHINE,  ///< Machine-readable directory listing.
      SIMPLE    ///< Simplified directory listing.
  };

  /** @enum Error
    * Enumerates library-related errors.
    **/
  enum Error {
    TIMEOUT = 1,  ///< for control channel
    INVARG = 2,   ///< (currently) applied if wrong enum value is passed
    BUSY = 3      ///< data transfer is underway / disconnect() on not connected client / connect() on connected client
  };

  /** @enum Status
    * Represents lib state. Used for multi-batch transactions.
    **/
  enum Status {
    DOWNLOADING, ///< after initDownload successfully executed
    UPLOADING, ///< after initUpload successfully executed
    IDLE
  };

  FTP32(const char* address, uint8_t port = 21) 
    : _address(address), _port(port), _ctrl_timeout_us(5e6), _data_timeout_us(_ctrl_timeout_us * 2){
    }


  // CONNECTION
  /** @brief connects to ftp server with username and password
    * 
    * @param[in] username user name
    * @param[in] password password
    * 
    * @see CommonReturnValues
    **/
  uint16_t connectWithPassword(const char* username, const char* password) {
    if( _cClient.connected() ) return Error::BUSY;
    FTP32_INFO("connecting as %s", username);
    if(!_cClient.connect(_address, _port, _ctrl_timeout_us/1e3) 
      || _readResponse() != 220
      || _sendCmd("USER", username, 331)
      || _sendCmd("PASS", password, 230))
    {
      FTP32_FATAL("connection failed %d %s", _r_code, _r_msg.c_str());
      return _r_code;   
    } else {
      FTP32_INFO("connected");
      return 0;
    }
  }

  /** @brief disconnects from FTP server closing all data and control connections
    * @see CommonReturnValues
    **/
  uint16_t disconnect() {
    if( !_cClient.connected() ) return Error::BUSY;

    uint16_t res = _sendCmd("QUIT", 221);
    _cClient.stop(); 
    FTP32_INFO("disconnected");

    return res;
  }


  // UPLOAD
  /** @brief initiates file transfer. 
    * Used to upload in multiple batches.
    * 
    * @param[in] destinationFilepath path to file that will be appended (created|overwritten) on server
    * @param[in] t @see TransferType
    * 
    * @see CommonReturnValues
    **/
  uint16_t initUpload(const char* destinationFilepath, OpenType t){
    if( _dClient.connected() || _status != IDLE ){ return Error::BUSY; }

    FTP32_INFO("initiating upload of %s", destinationFilepath);
    if( _openDataChn(_dClient) ){ return _r_code; }

    String cmd;
    switch(t){
      case OpenType::CREATE_REPLACE:
        cmd = "STOR";
        break;
      case OpenType::APPEND:
        cmd = "APPE";
        break;
      default:
        return Error::INVARG;
    }
    if( !_sendCmd(cmd.c_str(), destinationFilepath, 150) ){
      _status = UPLOADING;
      return 0;
    } else { 
      return _r_code; 
    }
  }

 /** @brief write data to the prevoulsy initiated upload transaction
    * 
    * @param[in] data should be null-terminated
    * 
    * @see CommonReturnValues
    * @note does nothing if called before initUpload()
    **/
  size_t uploadData(const char* data){
    if( _status != Status::UPLOADING ) return 0;

    auto written = _dClient.write(data);
    FTP32_INFO("%d written", written);

    return written;
  }

  /** @brief write data to the previously opened file
    * 
    * @param[in] data what's going to be writted
    * @param[in] size data size
    * 
    * @see CommonReturnValues
    * @note does nothing if called before commencing transmission
    * @overload uploadData(const char* data)
    **/
  size_t uploadData(uint8_t* data, size_t size){
    if( _status != Status::UPLOADING ) return 0;

    auto written = _dClient.write(data, size);
    FTP32_INFO("%d written", written);

    return written;
  }

  /** @brief finishes the upload transaction
    *  
    * @see CommonReturnValues
    * @note does nothing if called before commencing transmission
    **/
  uint16_t finishUpload(){
    if( _status != Status::UPLOADING ) return 0;

    _dClient.stop();
    _status = Status::IDLE;
    FTP32_INFO("upload fiished");

    return _readResponse() == 226 ? 0 : _r_code;
  }

  /** @brief uploads in a single transaction
    *  
    * @param destinationFilepath[in] path to the file that will be appended (created|overwritten) on server
    * @param data[in] data to transfer (should be null-terminated)
    * @param openType[in] append or overwrite
    * 
    * @see CommonReturnValues
    **/
  size_t uploadSingleshot(const char* destinationFilepath, const char* data, OpenType t){
    if( _status != Status::IDLE ) return Error::BUSY;
    if( initUpload(destinationFilepath, t) || !uploadData(data) || finishUpload() ){
      return _r_code;
    } else {
      return 0;
    }
  }

  /** @brief uploads file in a single transaction
    *  
    * @param destinationFilepath[in] path to the file that will be appended (created|overwritten) on server
    * @param data[in] data to transfer
    * @param dataSize[in] data size bruh
    * @param openType[in] transaction type
    * 
    * @see CommonReturnValues
    **/
  size_t uploadSingleshot(const char* destinationFilepath, uint8_t* data, size_t dataSize, OpenType t){
    if( _status != Status::IDLE ) return Error::BUSY;
    if( initUpload(destinationFilepath, t) || !uploadData(data, dataSize) || finishUpload() ){
      return _r_code;
    } else {
      return 0;
    }
  }


  // FILE UTILS
  /** @brief renames file
    * 
    * @param[in] from old name
    * @param[in] to new name
    * 
    * @see CommonReturnValues
    **/
  uint16_t renameFile(const char* from, const char* to){
    FTP32_INFO("renaming %s to %s", from, to);
    if( _sendCmd("RNFR", from, 350) || _sendCmd("RNTO", to, 250) ) return _r_code;
  }

   /** @brief deletes file
    * 
    * @param[in] filename name of the file
    * 
    * @see CommonReturnValues
    **/
  uint16_t deleteFile(const char* filename){
    FTP32_INFO("deleting %s", filename);
    return _sendCmd("DELE", filename, 250);
  }

  /** @brief retrieves file size
    * 
    * @param filepath[in] might be outside the current working dir
    * @param dest[out] value will be stored here
    * 
    * @see CommonReturnValues
    **/
  uint16_t fileSize(const char* filepath, size_t& dest){
    FTP32_INFO("getting size of %s", filepath);
    if( _sendCmd("SIZE", filepath, 213) ) return _r_code;
    dest = strtoul(_r_msg.c_str(), nullptr, 0);
    return 0;
  }

  // DOWNLOAD
  /** @brief initiates download transaction from the server
    * 
    * @param[in] filepath path to file
    * @note transaction is considered finished when downloadData() fullfils (!amount || (amount && read == 0))
    * @see CommonReturnValues
    **/
  uint16_t initDownload(const char* filename){
    if( _status != Status::IDLE ) return Error::BUSY;
    FTP32_INFO("initiating download of %s", filename);

    if( _openDataChn(_dClient) || _sendCmd("RETR", filename, 150) ){
      return _r_code;
    } else {
      _status = Status::DOWNLOADING;
      return 0;
    } 
  }

  /** @brief dowloads data from FTP server
    * 
    * @param dest[out] buffer for the data to be stored (memory should be pre-allocated)
    * @param amount[in] number of bytes do download. If ==0, all the available data will be downloaded.
    * 
    * @return number of bytes read
    **/
  size_t downloadData(char* dest, size_t amount = 0){
    if( _status != Status::DOWNLOADING ) return 0;
    size_t read = _readData(_dClient, dest, amount);
    FTP32_INFO("%d downloaded", read);
    if( !amount || (amount && read == 0) ){
      FTP32_INFO("download is finished");
      _readResponse(); 
      _status = Status::IDLE; 
    };
    return read;
  }

  /** @brief downloads the entire file from FTP server;
    * 
    * @param[in] filepath file to download
    * @param[out] dest place to load file content to
    * 
    * @see CommonReturnValues
    **/
  uint16_t downloadSingleshot(const char* filename, String& dest){
    if( _status != IDLE ){ return Error::BUSY; }

    FTP32_INFO("downloading %s", filename);
    if( _openDataChn(_dClient) || _sendCmd("RETR", filename, 150) ) return _r_code;

    _readData(_dClient, dest);

    return _readResponse() == 226 ? 0 : _r_code;
  }

  /** @brief downloads the entire file from FTP server;
    * 
    * @param[in] filepath file to download
    * @param[out] dest place to load file content to
    * @note memory should be pre-allocated
    * @see CommonReturnValues
    **/
  uint16_t downloadSingleshot(const char* filename, char* dest){
    if( _status != IDLE ){ return Error::BUSY; }

    FTP32_INFO("downloading %s", filename);
    if( _openDataChn(_dClient) || _sendCmd("RETR", filename, 150) ) return _r_code;

    _readData(_dClient, dest);

    return _readResponse() == 226 ? 0 : _r_code;
  }
  
  // DIR
  /** @brief creates new folder in the current working dir
    * 
    * @note won't create nested dirs.
    * 
    * @param[in] name name of the new directory
    * 
    * @see CommonReturnValues
    **/
  uint16_t mkdir(const char* name){
    FTP32_INFO("making dir %s", name);
    return _sendCmd("MKD", name, 257);
  }

  /** @brief Create a directory tree.
    * Creates a directory tree, and if a directory in the provided path already exists, it proceeds to create the rest of the tree.
    *
    * @param[in] path The complete tree path. Accepts paths with or without a trailing '/'.
    *
    * @see CommonReturnValues
    **/
  uint16_t mktree(const char* path) {
    String p(path);
    String sub;
    String pathContentBuff;

    String existingPath;

    int left{0}; 
    int right{0};

    if( p.startsWith("/") ){
      existingPath = "/";
      left = 1;
    } else {
      existingPath = "./";
    }

    while( existingPath != p ){
      if( listContent(existingPath.c_str(), ListType::SIMPLE, pathContentBuff) ) return _r_code;

      right = p.indexOf('/', left);
      String nextToAdd = p.substring(left, right);
      left = right + 1; // skip /
      existingPath += nextToAdd;
      if( right != -1 ) existingPath += "/";
      if( pathContentBuff.indexOf(existingPath) != -1 ) continue;
      if( mkdir(existingPath.c_str()) ) return _r_code;     
    }

    return 0;
  }

  /** @brief changes current working dir
    * 
    * @param[in] path desired path
    * 
    * @see CommonReturnValues
    **/
  uint16_t changeDir(const char* path){
    FTP32_INFO("changing cwd to %s", path);
    return _sendCmd("CWD", path, 250);
  }

  /** @brief removes an empty dir in the current working dir
    * 
    * @param[in] name name of the dir to remove
    * 
    * @see CommonReturnValues
    **/
  uint16_t rmdir(const char* name){
    FTP32_INFO("removing %s", name);
    return _sendCmd("RMD", name, 250);
  }

  /** @brief removes tree of directories regardless its content.
  * 
  * @param[in] path path to trees root
  * @note if "/" is provided as param, deletes all files, except the "/" itself
  * @note on INFO log lvl, it might spam a lot
  * @see CommonReturnValues
  **/
  uint16_t rmtree(const char* rootPath) {
    FTP32_INFO("removing tree %s", rootPath);
    std::stack<String> dirStack;
    String dirContent;
    String entry;
    String fullEntryPath;
    String currentDir(rootPath);
    
    if( currentDir[currentDir.length() - 1] == '/' )
      dirStack.emplace(currentDir.substring(0, currentDir.length() - 1));
    else
      dirStack.emplace(currentDir);
      
    while( !dirStack.empty() ) {
      currentDir = dirStack.top();
      if( currentDir.isEmpty() ){ dirStack.pop(); continue; } // some corner case

      dirContent = "";
      if( listContent(currentDir.c_str(), ListType::MACHINE, dirContent) ) return _r_code;

      if( dirContent.isEmpty() ){ // if top empty
        if( dirStack.size() == 1 && currentDir == "/" ) return 0; // prever / from being deleted
        if( rmdir(currentDir.c_str()) ){ return _r_code; }
        dirStack.pop();
        continue;
      }

      int startPos{};
      int endPos{};
      bool containDir{false};

      while (startPos < dirContent.length()) {
        endPos = dirContent.indexOf('\r', startPos); // each entry is followed by /r/n
        entry = dirContent.substring(startPos, endPos);
        fullEntryPath = currentDir + '/' + entry.substring(entry.indexOf(' ') + 1, endPos);

        startPos = endPos + 2; // +r+n

        if( entry.substring(entry.indexOf('=') + 1, entry.indexOf(';')) == "dir" ) {
          dirStack.push(fullEntryPath);
          containDir = true;
        } else {
          if( deleteFile(fullEntryPath.c_str()) ){ return _r_code; } 
        }
      }

      if( !containDir ){  // if newly checked dir is empty
        if( rmdir(currentDir.c_str()) ){ return _r_code; }
        dirStack.pop();
      }
    }

    return 0;
  }

  /** @brief retrieves current working directory
    * 
    * @param dest[out] result will stored here
    * 
    * @see CommonReturnValues
    **/
  uint16_t pwd(String& dest){
    FTP32_INFO("getting current dir");
    if( _sendCmd("PWD", 257) ) return _r_code;

    dest =  _r_msg.substring(_r_msg.indexOf('"') + 1,
                            _r_msg.lastIndexOf('"'));

    return 0;
  }


  // UTILS
  /** @brief returns content of the specified directory
    *
    * @param[in] dir path to directory 
    * @param[in] t list representation type
    * @param[out] dest result is gonna be stored here
    *
    * @see CommonReturnValues
    **/
  uint16_t listContent(const char* dir, ListType t, String& dest){
    FTP32_INFO("getting content of %s", dir);
    String cmd;
    switch(t){
      case ListType::HUMAN:
        cmd = "LIST";
        break;
      case ListType::MACHINE:
        cmd = "MLSD";
        break;
      case ListType::SIMPLE:
        cmd = "NLST";
        break;
      default:
        return Error::INVARG;
    }
    
    WiFiClient tmp;
    if( _openDataChn(tmp) ) return _r_code;

    if( _sendCmd(cmd.c_str(), dir, 150) ) return _r_code;

    _readData(tmp, dest);

    return _readResponse() == 226 ? 0 : _r_code;
  }

  /** @brief sets transfer type for both upload and download operations.
    *
    * The default transfer type is binary (TYPE I)
    *
    * @param[in] t transfer type to be used @see TransferType
    * 
    * @see CommonReturnValues
    **/
  uint16_t setTransferType(TransferType t){
    String type;
    switch(t){
      case TransferType::BINARY:
        type = "TYPE I";
        FTP32_INFO("setting transfer type to binary");
        break;
      case TransferType::ASCII:
        type = "TYPE A";
        FTP32_INFO("setting transfer type to ascii");
        break;
      default:
        return Error::INVARG;
    }

    return _sendCmd(type.c_str(), 200);
  }

  /** @brief returns the last time the file was modified.
    * 
    * @param[in] filename name of the file
    * @param[out] dest last modified date in YYYYMMDDHHMMSS.uuu
    *
    * @see CommonReturnValues
    **/
  uint16_t getLastModificationDate(const char* filename, String& date){
    FTP32_INFO("getting last modification date of %s", filename);
    return _sendCmd("MDTM", filename, 213);
  }

  /** @brief retrieves system info
    * 
    * @param[out] dest data will be stored here
    * 
    * @see CommonReturnValues
    **/
  uint16_t getSystemInfo(String& dest){
    FTP32_INFO("getting system info");
    if( _sendCmd("SYST", 215) ) return _r_code;
    dest = _r_msg;
    return 0;
  }

  // LIB CONFIG
  /** @brief sets the incoming control channel buffer max size.
    * @note some data like file size or data connectio address 
    * is returned through the control channel, so don't set it to 0; (and i didn't implement protection from you (: )
    * Calling this method won't affected data currently stored in input buffer.
    **/
  void setMaxInBufferSize(uint16_t size){
    _msg_buff_size = size;
  }

  /** @brief sets timeout for the data channel in milliseconds.
    *  Usually higher than for control channel
    **/ 
  void setDataChannelTimeout(uint16_t milliseconds){
    _data_timeout_us = milliseconds * 1e3;
  }
  
  /** @brief sets timeout for the control channel in milliseconds.
    * Usually lower than for data channel
    **/
  void setControlChannelTimeout(uint16_t milliseconds){
    _ctrl_timeout_us = milliseconds * 1e3;
  }
  

  // LIB DATA
  /** @return msg of the last response **/
  String getLastMsg(){
    return _r_msg;
  }

  /** @return code of the last response **/
  uint16_t getLastCode(){
    return _r_code;
  }

private:
  /** @brief Send command to FTP server. Checks for connection before sending.
    * @param[in] cmd The command to send.
    * @param[in] arg Command argument.
    * @param[in] expectedResponseCode The expected response code.
    * @see CommonReturnValues
    **/
  uint16_t _sendCmd(const char* cmd, const char* arg, uint16_t expectedResponseCode){
    if( !_cClient.connected() ) { _r_code = Error::TIMEOUT; return _r_code; }

    _cClient.println(String(cmd) + " " + String(arg));  

    if( _readResponse() == expectedResponseCode ){
      return 0;
    } else {
      FTP32_ERROR("%s %s FAILED %d %s", cmd, arg, _r_code, _r_msg.c_str());
      return _r_code;
    }
  }

  /** @brief Send command to FTP server. Checks for connection before sending.
    * @param[in] cmd The command to send.
    * @param[in] expectedResponseCode The expected response code.
    * @see CommonReturnValues
    **/
  uint16_t _sendCmd(const char* cmd, uint16_t expectedResponseCode){
    if( !_cClient.connected() ) { _r_code = Error::TIMEOUT; return _r_code; }
   
    _cClient.println(cmd);

    if( _readResponse() == expectedResponseCode ){
      return 0;
    } else {
      FTP32_ERROR("%s FAILED %d %s", cmd, _r_code, _r_msg.c_str());
      return _r_code;
    }
  }

  /** @brief Parses response data sent in the control channel.
    * 
    * Stores response code and response msg separately.
    * If the response msg is bigger than the buffer, trims it off.
    *
    * @return response code
    **/
  uint16_t _readResponse(){
    _r_msg = "";
    _r_code = 0;

    int i{};

    // read the response
    int64_t startTime = esp_timer_get_time();
    while( (esp_timer_get_time() - startTime) < _ctrl_timeout_us ){
      if( _cClient.available() ){
        char c = _cClient.read();
        switch( i++ ){ // O stands for oPTimZiaTion
          case 0: _r_code += (c - '0') * 100; break;
          case 1: _r_code += (c - '0') * 10; break;
          case 2: _r_code += (c - '0'); break;
          case 3: break; // skip trailing space
          default: // read msg
          {
            if( c == '\r' || i == _msg_buff_size ) { goto exit_loop; }
            _r_msg += c;
          }
        }
      } else {
        if( !_cClient.connected() ){ _r_code = Error::TIMEOUT; break; }
      }
    }
    exit_loop:;

    // discard the rest TODO
    while( _cClient.available() ) {
      _cClient.read();
    }

    return _r_code;
  }

  /** @brief reads data channel until timeout reached | data client is no longer connected | specified amount read
    *     
    * @tparam T type of input buffer. 
    * Tested on char*, String, std::string.
    * For raw pointer buffer, memory should be pre-allocated. 
    * For String& and std::string& data will be appended char-by-char;
    * 
    * @param[in] dataC WiFiclient to read
    * @param[out] dest String to append data to
    * @param[in] amount number of bytes to read
    *
    * @return number of bytes read.
    **/
  template<typename T>
  size_t _readData(WiFiClient& dataC, T& dest, size_t amount = 0){
    size_t read{0};
    int64_t startTime = esp_timer_get_time();
    while( (esp_timer_get_time() - startTime) < _data_timeout_us ) {
      if( amount && read == amount ) break;
      if( dataC.available() ) {
        add(dest, dataC.read(), read++);
      } else { 
        if( !dataC.connected() ) break;
      }
    }

    return read;
  }
  
  /** @brief establishes passive connection for data transmission.
    * 
    * @param[in] client the one is going to be used for data connection
    * 
    * @see CommonReturnValues
    **/
  uint16_t _openDataChn(WiFiClient& client){
    if( _sendCmd("PASV", 227) ) return _r_code;

    int startPos = _r_msg.indexOf("(");
    int endPos = _r_msg.indexOf(")");
    int parts[6]; // adress part 0-3 is ip, 4-5 is port
    if (startPos != -1 && endPos != -1) {
      String portStr = _r_msg.substring(startPos + 1, endPos);
      int partCount{};
      char* token = strtok(const_cast<char*>(portStr.c_str()), ",");
      while (token) {
        parts[partCount++] = atoi(token);
        token = strtok(NULL, ",");
      }
    }

    if( !client.connect(IPAddress(parts[0], parts[1], parts[2], parts[3]), (parts[4] << 8) | (parts[5] & 255), _ctrl_timeout_us/1e3) ){
      FTP32_ERROR("data connection cannot be established");
      return _r_code;
    } else {
      FTP32_INFO("data connection established");
      return 0;
    }
  }

  // overloads for differnt incoming data buffer types
  void add(String& str, char c, size_t pos){ str += c; }
  void add(char* str, char c, size_t pos){ str[pos] = c; }
  void add(std::string& str, char c, size_t pos){ str += c; }

private:
  WiFiClient _cClient;
  WiFiClient _dClient;
  Status _status{Status::IDLE};

  uint8_t _msg_buff_size{60};

  uint16_t _r_code;
  String _r_msg;

  const char* _address; 
  const uint8_t _port;
  
  // microseconds is too much resolution for user
  // but millis() is a bit slower and less reliable than esp_timer_get_time()
  // so set milsec, use microsec
  int64_t _ctrl_timeout_us;
  int64_t _data_timeout_us;
};

#endif // FTP32_H

#pragma once
#include <sys/epoll.h>
#include <unistd.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include "base/ChainBuffer.h"
#include "Timer.h"



class EventLoop;
class TimerNode;
class Channel;
class ChatRoom;

enum ProcessState {
  STATE_PARSE_URI = 1,
  STATE_PARSE_HEADERS,
  STATE_RECV_BODY,
  STATE_ANALYSIS,
  STATE_WEBSOCKET,
  STATE_FINISH
};

enum URIState {
  PARSE_URI_AGAIN = 1,
  PARSE_URI_ERROR,
  PARSE_URI_SUCCESS,
};

enum HeaderState {
  PARSE_HEADER_SUCCESS = 1,
  PARSE_HEADER_AGAIN,
  PARSE_HEADER_ERROR
};

enum AnalysisState { ANALYSIS_SUCCESS = 1, ANALYSIS_ERROR=2,ANALYSIS_AGAIN = 3 };

enum ParseState {
  H_START = 0,
  H_KEY,
  H_COLON,
  H_SPACES_AFTER_COLON,
  H_VALUE,
  H_CR,
  H_LF,
  H_END_CR,
  H_END_LF
};

enum ConnectionState { H_CONNECTED = 0, H_DISCONNECTING, H_DISCONNECTED };

enum HttpMethod { METHOD_POST = 1, METHOD_GET, METHOD_HEAD };

enum HttpVersion { HTTP_10 = 1, HTTP_11 };



class HttpData : public std::enable_shared_from_this<HttpData> {
 public:
  HttpData(EventLoop *loop, int connfd);
  ~HttpData() {
    if (fd_ >= 0) close(fd_);
  }
  void reset();
  void seperateTimer();
  void linkTimer(std::shared_ptr<TimerNode> mtimer) {
    timer_ = mtimer;
  }
  std::shared_ptr<Channel> getChannel() { return channel_; }
  EventLoop *getLoop() { return loop_; }
  void handleClose();
  void newEvent();
  ProcessState getRequestStatus();
  ChainBuffer& getInBuffer() { return inBuffer_; }
  ChainBuffer& getOutBuffer();
  void ctlHandleWrite();
  void sendResponse(int status, const std::string& type, const std::string& body);
  int getFd() {return fd_;}
  void sendMsg(const std::string& msg);
   void init();
   
  private:
  EventLoop *loop_;
  std::shared_ptr<Channel> channel_;
  int fd_;
  ChainBuffer inBuffer_;
  ChainBuffer outBuffer_;
  std::string responseHeader_;
  std::string   responseBody_; 
  bool error_;
  bool closed_;
  ConnectionState connectionState_;

  size_t headerSendIdx_;
  size_t bodySendIdx_;
  HttpMethod method_;
  HttpVersion HTTPVersion_;
  std::string fileName_;
  std::string path_;
  std::string uri_; // 请求路径 
  std::string query_; // URL 参数
  int nowReadPos_;
  ProcessState state_;
  ParseState hState_;
  bool keepAlive_;
  std::unordered_map<std::string, std::string> headers_;  // changed from map: O(1) vs O(logN) per lookup
  std::weak_ptr<TimerNode> timer_;
  std::unordered_map<int, std::shared_ptr<ChatRoom>> rooms_;
  
  void handleRead();
  void handleReadComplete(int bytes_read);
  void handleWriteComplete(int bytes_written);
  void submitAsyncRead();
  void submitAsyncWrite();
  bool isWriting_;
  void handleWrite();
  void handleConn();
  void handleError(int fd, int err_num, std::string short_msg);
  void handleWebSocketFrame();
  void broadcastMessage(const std::string& msg, HttpData* sender);
  void handleWriteInLoop(const std::string& msg);
  AnalysisState handleStaticFile();
  std::string getQueryParam(const std::string& key);

  URIState parseURI();
  HeaderState parseHeaders();
  AnalysisState analysisRequest();
  AnalysisState handleWebSocketHandshake();
};

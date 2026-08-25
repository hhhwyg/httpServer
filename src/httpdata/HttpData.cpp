#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string_view>
#include "HttpData.h"
#include "httpserver/transport/EventLoop.h"
#include "Util.h"
#include "time.h"
#include "CryptoUtil.h"
#include "httpserver/application/Authentication.h"
#include "httpserver/application/ApplicationRouter.h"
#include "httpserver/application/ChatApplication.h"
#include "httpserver/protocol/HttpRequest.h"
#include "httpserver/protocol/HttpResponse.h"
#include "httpserver/protocol/WebSocket.h"
const __uint32_t DEFAULT_EVENT = EPOLLIN | EPOLLET | EPOLLONESHOT;
const int DEFAULT_EXPIRED_TIME = 2000;              // ms
const int DEFAULT_KEEP_ALIVE_TIME = 5 * 60 * 1000;  // ms
constexpr std::size_t kMaxRequestLineBytes = 8192;
constexpr std::size_t kMaxHeaderBytes = 16384;
constexpr std::size_t kMaxBodyBytes = 1024 * 1024;
constexpr std::size_t kMaxWebSocketPayloadBytes = 1024 * 1024;

namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool hasPathTraversal(const std::string& path) {
  if (path.empty() || path.front() != '/' || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos) {
    return true;
  }
  std::size_t begin = 1;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::string segment = path.substr(begin, end - begin);
    if (segment == "..") return true;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return false;
}

}  // namespace


char favicon[555] = {
    '\x89', 'P',    'N',    'G',    '\xD',  '\xA',  '\x1A', '\xA',  '\x0',
    '\x0',  '\x0',  '\xD',  'I',    'H',    'D',    'R',    '\x0',  '\x0',
    '\x0',  '\x10', '\x0',  '\x0',  '\x0',  '\x10', '\x8',  '\x6',  '\x0',
    '\x0',  '\x0',  '\x1F', '\xF3', '\xFF', 'a',    '\x0',  '\x0',  '\x0',
    '\x19', 't',    'E',    'X',    't',    'S',    'o',    'f',    't',
    'w',    'a',    'r',    'e',    '\x0',  'A',    'd',    'o',    'b',
    'e',    '\x20', 'I',    'm',    'a',    'g',    'e',    'R',    'e',
    'a',    'd',    'y',    'q',    '\xC9', 'e',    '\x3C', '\x0',  '\x0',
    '\x1',  '\xCD', 'I',    'D',    'A',    'T',    'x',    '\xDA', '\x94',
    '\x93', '9',    'H',    '\x3',  'A',    '\x14', '\x86', '\xFF', '\x5D',
    'b',    '\xA7', '\x4',  'R',    '\xC4', 'm',    '\x22', '\x1E', '\xA0',
    'F',    '\x24', '\x8',  '\x16', '\x16', 'v',    '\xA',  '6',    '\xBA',
    'J',    '\x9A', '\x80', '\x8',  'A',    '\xB4', 'q',    '\x85', 'X',
    '\x89', 'G',    '\xB0', 'I',    '\xA9', 'Q',    '\x24', '\xCD', '\xA6',
    '\x8',  '\xA4', 'H',    'c',    '\x91', 'B',    '\xB',  '\xAF', 'V',
    '\xC1', 'F',    '\xB4', '\x15', '\xCF', '\x22', 'X',    '\x98', '\xB',
    'T',    'H',    '\x8A', 'd',    '\x93', '\x8D', '\xFB', 'F',    'g',
    '\xC9', '\x1A', '\x14', '\x7D', '\xF0', 'f',    'v',    'f',    '\xDF',
    '\x7C', '\xEF', '\xE7', 'g',    'F',    '\xA8', '\xD5', 'j',    'H',
    '\x24', '\x12', '\x2A', '\x0',  '\x5',  '\xBF', 'G',    '\xD4', '\xEF',
    '\xF7', '\x2F', '6',    '\xEC', '\x12', '\x20', '\x1E', '\x8F', '\xD7',
    '\xAA', '\xD5', '\xEA', '\xAF', 'I',    '5',    'F',    '\xAA', 'T',
    '\x5F', '\x9F', '\x22', 'A',    '\x2A', '\x95', '\xA',  '\x83', '\xE5',
    'r',    '9',    'd',    '\xB3', 'Y',    '\x96', '\x99', 'L',    '\x6',
    '\xE9', 't',    '\x9A', '\x25', '\x85', '\x2C', '\xCB', 'T',    '\xA7',
    '\xC4', 'b',    '1',    '\xB5', '\x5E', '\x0',  '\x3',  'h',    '\x9A',
    '\xC6', '\x16', '\x82', '\x20', 'X',    'R',    '\x14', 'E',    '6',
    'S',    '\x94', '\xCB', 'e',    'x',    '\xBD', '\x5E', '\xAA', 'U',
    'T',    '\x23', 'L',    '\xC0', '\xE0', '\xE2', '\xC1', '\x8F', '\x0',
    '\x9E', '\xBC', '\x9',  'A',    '\x7C', '\x3E', '\x1F', '\x83', 'D',
    '\x22', '\x11', '\xD5', 'T',    '\x40', '\x3F', '8',    '\x80', 'w',
    '\xE5', '3',    '\x7',  '\xB8', '\x5C', '\x2E', 'H',    '\x92', '\x4',
    '\x87', '\xC3', '\x81', '\x40', '\x20', '\x40', 'g',    '\x98', '\xE9',
    '6',    '\x1A', '\xA6', 'g',    '\x15', '\x4',  '\xE3', '\xD7', '\xC8',
    '\xBD', '\x15', '\xE1', 'i',    '\xB7', 'C',    '\xAB', '\xEA', 'x',
    '\x2F', 'j',    'X',    '\x92', '\xBB', '\x18', '\x20', '\x9F', '\xCF',
    '3',    '\xC3', '\xB8', '\xE9', 'N',    '\xA7', '\xD3', 'l',    'J',
    '\x0',  'i',    '6',    '\x7C', '\x8E', '\xE1', '\xFE', 'V',    '\x84',
    '\xE7', '\x3C', '\x9F', 'r',    '\x2B', '\x3A', 'B',    '\x7B', '7',
    'f',    'w',    '\xAE', '\x8E', '\xE',  '\xF3', '\xBD', 'R',    '\xA9',
    'd',    '\x2',  'B',    '\xAF', '\x85', '2',    'f',    'F',    '\xBA',
    '\xC',  '\xD9', '\x9F', '\x1D', '\x9A', 'l',    '\x22', '\xE6', '\xC7',
    '\x3A', '\x2C', '\x80', '\xEF', '\xC1', '\x15', '\x90', '\x7',  '\x93',
    '\xA2', '\x28', '\xA0', 'S',    'j',    '\xB1', '\xB8', '\xDF', '\x29',
    '5',    'C',    '\xE',  '\x3F', 'X',    '\xFC', '\x98', '\xDA', 'y',
    'j',    'P',    '\x40', '\x0',  '\x87', '\xAE', '\x1B', '\x17', 'B',
    '\xB4', '\x3A', '\x3F', '\xBE', 'y',    '\xC7', '\xA',  '\x26', '\xB6',
    '\xEE', '\xD9', '\x9A', '\x60', '\x14', '\x93', '\xDB', '\x8F', '\xD',
    '\xA',  '\x2E', '\xE9', '\x23', '\x95', '\x29', 'X',    '\x0',  '\x27',
    '\xEB', 'n',    'V',    'p',    '\xBC', '\xD6', '\xCB', '\xD6', 'G',
    '\xAB', '\x3D', 'l',    '\x7D', '\xB8', '\xD2', '\xDD', '\xA0', '\x60',
    '\x83', '\xBA', '\xEF', '\x5F', '\xA4', '\xEA', '\xCC', '\x2',  'N',
    '\xAE', '\x5E', 'p',    '\x1A', '\xEC', '\xB3', '\x40', '9',    '\xAC',
    '\xFE', '\xF2', '\x91', '\x89', 'g',    '\x91', '\x85', '\x21', '\xA8',
    '\x87', '\xB7', 'X',    '\x7E', '\x7E', '\x85', '\xBB', '\xCD', 'N',
    'N',    'b',    't',    '\x40', '\xFA', '\x93', '\x89', '\xEC', '\x1E',
    '\xEC', '\x86', '\x2',  'H',    '\x26', '\x93', '\xD0', 'u',    '\x1D',
    '\x7F', '\x9',  '2',    '\x95', '\xBF', '\x1F', '\xDB', '\xD7', 'c',
    '\x8A', '\x1A', '\xF7', '\x5C', '\xC1', '\xFF', '\x22', 'J',    '\xC3',
    '\x87', '\x0',  '\x3',  '\x0',  'K',    '\xBB', '\xF8', '\xD6', '\x2A',
    'v',    '\x98', 'I',    '\x0',  '\x0',  '\x0',  '\x0',  'I',    'E',
    'N',    'D',    '\xAE', 'B',    '\x60', '\x82',
};
void HttpData::sendResponse(int status, const std::string& type, const std::string& body) {
    outBuffer_.append(httpserver::HttpResponseWriter::SerializeHead(
        status, type, body.size(), keepAlive_));
    if (method_ != METHOD_HEAD) {
      outBuffer_.append(body);
    }
    submitAsyncWrite();
}

void HttpData::handleWriteInLoop(const std::string& msg) {
    if (error_ || connectionState_ == H_DISCONNECTED) return;

    // 统一追加到缓冲区末尾
    outBuffer_.append(msg); 

    // 尝试立即发送
    submitAsyncWrite();
}

void HttpData::sendMsg(const std::string& msg) {
    // 1. 延长对象生命周期，确保异步任务执行时对象不过期
    auto self = shared_from_this(); 
    
    // 2. 将任务丢进所属 Loop 的队列，确保线程安全
    loop_->runInLoop([self, msg]() {
        self->handleWriteInLoop(
            httpserver::WebSocketCodec::EncodeFrame(0x1, msg));
    });
}

void HttpData::closeWebSocket(std::uint16_t code, const std::string& reason) {
  if (closeAfterWrite_ || closed_) return;
  std::string payload;
  payload.push_back(static_cast<char>((code >> 8) & 0xff));
  payload.push_back(static_cast<char>(code & 0xff));
  payload.append(reason.substr(0, 123));
  closeAfterWrite_ = true;
  connectionState_ = H_DISCONNECTING;
  handleWriteInLoop(httpserver::WebSocketCodec::EncodeFrame(0x8, payload));
}

 ProcessState HttpData::getRequestStatus(){
  return state_;
 }
  ChainBuffer& HttpData::getOutBuffer(){
    return outBuffer_;
  }
  void HttpData::ctlHandleWrite(){
    submitAsyncWrite();
  }


HttpData::HttpData(httpserver::EventLoop* loop, int connfd)
    : loop_(loop),
      channel_(new httpserver::Channel(loop, connfd)),
      fd_(connfd),
      error_(false),
      closed_(false),
      connectionState_(H_CONNECTED),
      headerSendIdx_(0),
      bodySendIdx_(0),
      method_(METHOD_GET),
      HTTPVersion_(HTTP_11),
      nowReadPos_(0),
      state_(STATE_PARSE_URI),
      hState_(H_START),
      keepAlive_(false),
      isReading_(false),
      isWriting_(false) {
  // loop_->queueInLoop(bind(&HttpData::setHandlers, this));
  //channel_->setReadHandler(bind(&HttpData::handleRead, this));
  //channel_->setWriteHandler(bind(&HttpData::handleWrite, this));
  //channel_->setConnHandler(bind(&HttpData::handleConn, this));
}


void HttpData::init() {
  const std::weak_ptr<HttpData> weakSelf = shared_from_this();
  channel_->setReadHandler([weakSelf] {
    if (const auto self = weakSelf.lock()) {
      self->handleRead();
    }
  });
  channel_->setWriteHandler([weakSelf] {
    if (const auto self = weakSelf.lock()) {
      self->handleWrite();
    }
  });
  channel_->setConnHandler([weakSelf] {
    if (const auto self = weakSelf.lock()) {
      self->handleConn();
    }
  });
  channel_->setAsyncReadHandler([weakSelf](int bytesRead) {
    if (const auto self = weakSelf.lock()) {
      self->handleReadComplete(bytesRead);
    }
  });
  channel_->setAsyncWriteHandler([weakSelf](int bytesWritten) {
    if (const auto self = weakSelf.lock()) {
      self->handleWriteComplete(bytesWritten);
    }
  });
}
void HttpData::reset() {
  // inBuffer_.clear();
  fileName_.clear();
  path_.clear();
  nowReadPos_ = 0;
  state_ = STATE_PARSE_URI;
  hState_ = H_START;
  headers_.clear();
  contentLength_ = 0;
  hasContentLength_ = false;
  webSocketFragment_.clear();
  webSocketFragmented_ = false;
  closeAfterWrite_ = false;
  // keepAlive_ = false;
  if (timer_.lock()) {
    std::shared_ptr<TimerNode> my_timer(timer_.lock());
    my_timer->clearReq();
    timer_.reset();
  }
}

void HttpData::seperateTimer() {
  // cout << "seperateTimer" << endl;
  if (timer_.lock()) {
    std::shared_ptr<TimerNode> my_timer(timer_.lock());
    my_timer->clearReq();
    timer_.reset();
  }
}

void HttpData::handleRead() {
  __uint32_t &events_ = channel_->getEvents();
    do {
        int savedErrno = 0;
        int read_num = inBuffer_.readFd(fd_, &savedErrno);
        bool zero = (read_num == 0);
        /*
        std::cout << "[handleRead] read_num=" << read_num 
          << " zero=" << zero 
          << " inBuffer=[" << inBuffer_ << "]" << std::endl;
          */


        if (connectionState_ == H_DISCONNECTING) {
            inBuffer_.clear();
            break;
        }

        if (read_num < 0) {
            if (savedErrno == EAGAIN) {
                // 读取完毕，等待下次可读事件
                break;
            }
            perror("readn");
            error_ = true;
            handleError(fd_, 400, "Bad Request");
            break;
        }
        else if (zero) {
            connectionState_ = H_DISCONNECTING;
            break;
        }

        if (inBuffer_.readableBytes() >
            static_cast<int>(kMaxHeaderBytes + kMaxBodyBytes)) {
            error_ = true;
            handleError(fd_, 413, "Payload Too Large");
            break;
        }

        if (state_ == STATE_WEBSOCKET) {
            handleWebSocketFrame();
            break; // WebSocket 处理完或数据不足，直接跳出等待下一次 EPOLLIN
        }

        // --- 状态机逻辑 ---

        // 1. 解析 URI
        if (state_ == STATE_PARSE_URI) {
            URIState flag = this->parseURI();
            if (flag == PARSE_URI_AGAIN)
                break;
            else if (flag == PARSE_URI_ERROR) {
                perror("2");
                inBuffer_.clear();
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                break;
            } else
                state_ = STATE_PARSE_HEADERS;
        }

        // 2. 解析头部
        if (state_ == STATE_PARSE_HEADERS) {
            HeaderState flag = this->parseHeaders();
            if (flag == PARSE_HEADER_AGAIN)
                break;
            else if (flag == PARSE_HEADER_ERROR) {
                perror("3");
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                break;
            }
            
            if (method_ == METHOD_POST) {
                state_ = STATE_RECV_BODY;
            } else {
                state_ = STATE_ANALYSIS;
            }
        }

        // 3. 接收 Body (这里是之前出问题最多的地方)
        if (state_ == STATE_RECV_BODY) {
            if (!hasContentLength_) {
                // 如果找不到 Content-Length，报错
                error_ = true;
                handleError(fd_, 400, "Bad Request: Lack of argument (Content-length)");
                break;
            }

            // [关键] 检查数据有没有收全
            // 注意：inBuffer_ 此时包含了整个 body 的数据
            if (inBuffer_.readableBytes() < static_cast<int>(contentLength_)) {
                // 数据不够，跳出循环，等待下一次 epoll 事件（ANALYSIS_AGAIN 的效果）
                break; 
            }
            
            // 数据收齐了，进入处理阶段
            state_ = STATE_ANALYSIS;
        }

        // 4. 业务逻辑处理 (OpenCV 在这里)
        if (state_ == STATE_ANALYSIS) {
            AnalysisState flag = this->analysisRequest();
            if (flag == ANALYSIS_SUCCESS) {
                if (state_ != STATE_WEBSOCKET) {
                    state_ = STATE_FINISH;
                }
                break;
            } else if (flag == ANALYSIS_AGAIN) {
                // 如果 analysisRequest 内部觉得数据不够（双重保险），也 break 等待
                break;
            } else {
                error_ = true;
                break;
            }
        }
    } while (false);
  // cout << "state_=" << state_ << endl;
  if (!error_) {
    if (!outBuffer_.empty()) {
      submitAsyncWrite();
      // events_ |= EPOLLOUT;
    }
    // error_ may change
    if (!error_ && state_ == STATE_FINISH) {
      this->reset();
      if (!inBuffer_.empty()) {
        if (connectionState_ != H_DISCONNECTING) handleRead();
      }

      // if ((keepAlive_ || !inBuffer_.empty()) && connectionState_ ==
      // H_CONNECTED)
      // {
      //     this->reset();
      //     events_ |= EPOLLIN;
      // }
    } else if (!error_ && connectionState_ != H_DISCONNECTED)
      events_ |= EPOLLIN;
  }
}

void HttpData::handleWebSocketFrame() {
  while (true) {
    const std::string input = inBuffer_.peekAllAsString();
    if (input.size() < 2) return;

    const auto decoded = httpserver::WebSocketCodec::DecodeFrame(input);
    if (decoded.status ==
        httpserver::WebSocketDecodeStatus::kNeedMoreData) {
      return;
    }
    if (decoded.status == httpserver::WebSocketDecodeStatus::kProtocolError) {
      closeWebSocket(decoded.closeCode, decoded.errorReason);
      return;
    }

    const bool fin = decoded.frame.fin;
    const unsigned char opcode = decoded.frame.opcode;
    std::string payload = decoded.frame.payload;
    inBuffer_.retrieve(static_cast<int>(decoded.frame.bytesConsumed));

    if (opcode == 0x8) {
      if (payload.size() == 1) {
        closeWebSocket(1002, "invalid close payload");
      } else {
        closeAfterWrite_ = true;
        connectionState_ = H_DISCONNECTING;
        handleWriteInLoop(
            httpserver::WebSocketCodec::EncodeFrame(0x8, payload));
      }
      return;
    }
    if (opcode == 0x9) {
      handleWriteInLoop(
          httpserver::WebSocketCodec::EncodeFrame(0xA, payload));
      continue;
    }
    if (opcode == 0xA) continue;

    if (opcode == 0x2) {
      closeWebSocket(1003, "binary messages unsupported");
      return;
    }
    if (opcode == 0x0) {
      if (!webSocketFragmented_) {
        closeWebSocket(1002, "unexpected continuation");
        return;
      }
    } else if (opcode == 0x1) {
      if (webSocketFragmented_) {
        closeWebSocket(1002, "nested fragment");
        return;
      }
      if (!fin) {
        webSocketFragmented_ = true;
        webSocketFragment_.clear();
      }
    } else {
      closeWebSocket(1002, "unknown opcode");
      return;
    }

    if (!fin || webSocketFragmented_) {
      webSocketFragment_.append(payload);
      if (webSocketFragment_.size() > kMaxWebSocketPayloadBytes) {
        closeWebSocket(1009, "message too large");
        return;
      }
      if (!fin) continue;
      payload.swap(webSocketFragment_);
      webSocketFragmented_ = false;
    }

    const auto self = shared_from_this();
    httpserver::ChatApplication::HandleMessage(
        payload, fd_, self,
        [self](const std::string& message) { self->sendMsg(message); });
  }
}


void HttpData::handleWrite() {
    if (error_ || connectionState_ == H_DISCONNECTED) return;
    if (outBuffer_.empty()) return;

    // 1. 调用系统写函数
    int savedErrno = 0;
    ssize_t n = outBuffer_.writeFd(fd_, &savedErrno);

    if (n < 0 && savedErrno != EAGAIN) {
        error_ = true;
        handleClose();
        return;
    }

    // 3. 事件管理
    uint32_t &events = channel_->getEvents();
    if (!outBuffer_.empty()) {
        // 数据没发完，注册写事件等待内核通知
        if (!(events & EPOLLOUT)) {
            events |= EPOLLOUT;
            loop_->updatePoller(channel_);
        }
    } else {
        // 发完了，移除写事件，防止忙轮询
        if (events & EPOLLOUT) {
            events &= ~EPOLLOUT;
            loop_->updatePoller(channel_);
        }
        // 如果是短连接，发完即关
        if (closeAfterWrite_ || (!keepAlive_ && connectionState_ == H_DISCONNECTING)) {
            handleClose();
        }
    }
}

void HttpData::handleConn() {

  seperateTimer();
  __uint32_t &events_ = channel_->getEvents();
  if (!error_ && connectionState_ == H_CONNECTED) {
    if (events_ != 0) {
      int timeout = DEFAULT_EXPIRED_TIME;
      if (keepAlive_) timeout = DEFAULT_KEEP_ALIVE_TIME;
      if ((events_ & EPOLLIN) && (events_ & EPOLLOUT)) {
        events_ = __uint32_t(0);
        events_ |= EPOLLOUT;
      }
      // events_ |= (EPOLLET | EPOLLONESHOT);
      events_ |= EPOLLET;
      loop_->updatePoller(channel_, timeout);

    } else if (keepAlive_) {
      events_ |= (EPOLLIN | EPOLLET);
      // events_ |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
      int timeout = DEFAULT_KEEP_ALIVE_TIME;
      loop_->updatePoller(channel_, timeout);
    } else {
      // cout << "close normally" << endl;
      // loop_->shutdown(channel_);
      // loop_->runInLoop(bind(&HttpData::handleClose, shared_from_this()));
      events_ |= (EPOLLIN | EPOLLET);
      // events_ |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
      int timeout = (DEFAULT_KEEP_ALIVE_TIME >> 1);
      loop_->updatePoller(channel_, timeout);
    }
  } else if (!error_ && connectionState_ == H_DISCONNECTING &&
             (events_ & EPOLLOUT)) {
    events_ = (EPOLLOUT | EPOLLET);
  } else {
    // cout << "close with errors" << endl;
    loop_->runInLoop(std::bind(&HttpData::handleClose, shared_from_this()));
  }
}

URIState HttpData::parseURI() {
  const std::string input = inBuffer_.peekAllAsString();
  const std::size_t end = input.find("\r\n");
  if (end == std::string::npos) {
    return input.size() > kMaxRequestLineBytes ? PARSE_URI_ERROR : PARSE_URI_AGAIN;
  }
  if (end == 0 || end > kMaxRequestLineBytes) return PARSE_URI_ERROR;

  const auto parsed = httpserver::HttpRequestParser::ParseRequestLine(
      std::string_view(input).substr(0, end));
  if (!parsed.has_value()) return PARSE_URI_ERROR;

  if (parsed->method == "GET") method_ = METHOD_GET;
  else if (parsed->method == "POST") method_ = METHOD_POST;
  else if (parsed->method == "HEAD") method_ = METHOD_HEAD;
  else return PARSE_URI_ERROR;

  if (parsed->version == "HTTP/1.1") HTTPVersion_ = HTTP_11;
  else if (parsed->version == "HTTP/1.0") HTTPVersion_ = HTTP_10;
  else return PARSE_URI_ERROR;

  const std::size_t queryStart = parsed->target.find('?');
  uri_ = parsed->target.substr(0, queryStart);
  query_ = queryStart == std::string::npos
               ? ""
               : parsed->target.substr(queryStart + 1);
  if (hasPathTraversal(uri_)) return PARSE_URI_ERROR;
  path_ = uri_;
  fileName_ = uri_ == "/" ? "index.html" : uri_.substr(1);
  inBuffer_.retrieve(static_cast<int>(end + 2));
  return PARSE_URI_SUCCESS;
}


HeaderState HttpData::parseHeaders() {
  const std::string input = inBuffer_.peekAllAsString();
  const std::size_t terminator = input.find("\r\n\r\n");
  if (terminator == std::string::npos) {
    return input.size() > kMaxHeaderBytes ? PARSE_HEADER_ERROR : PARSE_HEADER_AGAIN;
  }
  if (terminator > kMaxHeaderBytes) return PARSE_HEADER_ERROR;

  const auto parsed = httpserver::HttpRequestParser::ParseHeaders(
      std::string_view(input).substr(0, terminator),
      HTTPVersion_ == HTTP_11 ? "HTTP/1.1" : "HTTP/1.0");
  if (!parsed.has_value()) return PARSE_HEADER_ERROR;

  headers_ = parsed->values;
  if (parsed->contentLength.has_value()) {
    contentLength_ = *parsed->contentLength;
    hasContentLength_ = true;
  }
  inBuffer_.retrieve(static_cast<int>(terminator + 4));
  keepAlive_ = parsed->keepAlive;
  return PARSE_HEADER_SUCCESS;
}

AnalysisState HttpData::handleWebSocketHandshake() {

    // 1. 必须是 WebSocket 升级请求
    if (headers_.find("upgrade") == headers_.end() ||
        headers_.find("sec-websocket-key") == headers_.end()) {
        handleError(fd_, 400, "Bad WebSocket Handshake");
        return ANALYSIS_ERROR;
    }

    // 2. 检查 Upgrade 字段
    const std::string upgrade_val = toLower(headers_["upgrade"]);
    const auto connection = headers_.find("connection");
    if (upgrade_val != "websocket" || connection == headers_.end() ||
        toLower(connection->second).find("upgrade") == std::string::npos) {
        handleError(fd_, 400, "Not WebSocket");
        return ANALYSIS_ERROR;
    }
    const auto version = headers_.find("sec-websocket-version");
    if (version == headers_.end() || version->second != "13") {
        handleError(fd_, 400, "Unsupported WebSocket Version");
        return ANALYSIS_ERROR;
    }

    // 3. JWT 鉴权（从 URL query 中取 token）
    const auto username = httpserver::Authenticator::VerifyQuery(query_);
    if (!username.has_value()) {
        handleError(fd_, 401, "Unauthorized");
        return ANALYSIS_ERROR;
    }

    // The verified subject is available in *username for future session binding.

    // 5. 计算 Accept Key
    const std::string clientKey = headers_["sec-websocket-key"];
    std::string acceptKey = CryptoUtil::computeAcceptKey(clientKey);

    // 6. 构造握手响应
    std::string header;
    header += "HTTP/1.1 101 Switching Protocols\r\n";
    header += "Upgrade: websocket\r\n";
    header += "Connection: Upgrade\r\n";
    header += "Sec-WebSocket-Accept: " + acceptKey + "\r\n";
    header += "\r\n";

    outBuffer_.append(header);

    // 7. 切换状态机
    state_ = STATE_WEBSOCKET;
    keepAlive_ = true;

    // 8. 发送握手响应
    submitAsyncWrite();

    return ANALYSIS_SUCCESS;
}

AnalysisState HttpData::analysisRequest() {
    if (method_ == METHOD_GET && uri_ == "/ws") {
        return handleWebSocketHandshake();
    }

    std::string_view method;
    switch (method_) {
      case METHOD_GET:
        method = "GET";
        break;
      case METHOD_POST:
        method = "POST";
        break;
      case METHOD_HEAD:
        method = "HEAD";
        break;
    }
    if (httpserver::ApplicationRouter::Dispatch(
            shared_from_this(), method, uri_, query_, headers_) ==
        httpserver::RouteResult::kHandled) {
        return ANALYSIS_SUCCESS;
    }

    if (method_ == METHOD_GET || method_ == METHOD_HEAD) {
        return handleStaticFile();
    }

    handleError(fd_, 405, "Method Not Allowed");
    return ANALYSIS_ERROR;
}


AnalysisState HttpData::handleStaticFile() {
  const auto file = staticFileService_.load(uri_, method_ == METHOD_GET);
  if (file.status == httpserver::StaticFileStatus::kNotFound) {
    handleError(fd_, 404, "Not Found");
    return ANALYSIS_SUCCESS;
  }
  if (file.status == httpserver::StaticFileStatus::kTooLarge) {
    handleError(fd_, 413, "File Too Large");
    return ANALYSIS_SUCCESS;
  }

  outBuffer_.append(httpserver::HttpResponseWriter::SerializeHead(
      200, file.contentType, file.contentLength, keepAlive_));
  if (method_ == METHOD_GET) outBuffer_.append(file.body);
  submitAsyncWrite();
  return ANALYSIS_SUCCESS;
}

void HttpData::handleError(int fd, int err_num, std::string short_msg) {
  short_msg = " " + short_msg;
  char send_buff[4096];
  std::string body_buff, header_buff;
  body_buff += "<html><title>哎~出错了</title>";
  body_buff += "<body bgcolor=\"ffffff\">";
  body_buff += std::to_string(err_num) + short_msg;
  body_buff += "<hr><em> LinYa's Web Server</em>\n</body></html>";

  header_buff += "HTTP/1.1 " + std::to_string(err_num) + short_msg + "\r\n";
  header_buff += "Content-Type: text/html\r\n";
  header_buff += "Connection: Close\r\n";
  header_buff += "Content-Length: " + std::to_string(body_buff.size()) + "\r\n";
  header_buff += "Server: LinYa's Web Server\r\n";
  ;
  header_buff += "\r\n";
  // 错误处理不考虑writen不完的情况
  sprintf(send_buff, "%s", header_buff.c_str());
  writen(fd, send_buff, strlen(send_buff));
  if (method_ != METHOD_HEAD) {
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
  }
}

void HttpData::handleClose() {
  if (closed_) return;
  closed_ = true;
  connectionState_ = H_DISCONNECTED;

  // Keep the object alive while detaching the timer. TimerNode owns a shared
  // pointer to HttpData and clearReq() can otherwise release the final owner.
  const std::shared_ptr<HttpData> guard(shared_from_this());
  if (channel_) {
    if (loop_) {
      loop_->removeFromPoller(channel_);
    } else {
      channel_->deactivate();
    }
    channel_->clearHolder();
  }
  seperateTimer();
  if (fd_ >= 0) {
    httpserver::ChatApplication::Leave(fd_);
  }
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

void HttpData::newEvent() {
  if (closed_ || !channel_) return;
  channel_->setEvents(DEFAULT_EVENT);
  loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
  
}

void HttpData::submitAsyncRead() {
    if (closed_ || connectionState_ == H_DISCONNECTED || isReading_) return;
    char* buf = nullptr;
    int len = 0;
    inBuffer_.getWritableChunkInfo(&buf, &len);
    if (buf && len > 0) {
        isReading_ = true;
        loop_->submitRead(channel_, buf, len);
    }
}

void HttpData::submitAsyncWrite() {
    if (closed_ || connectionState_ == H_DISCONNECTED || isWriting_) return;
    
    char* buf = nullptr;
    int len = 0;
    outBuffer_.getReadableChunkInfo(&buf, &len);
    if (buf && len > 0) {
        isWriting_ = true;
        loop_->submitWrite(channel_, buf, len);
    } else {
        // finished writing
        if (state_ == STATE_FINISH) {
            if (keepAlive_) {
                reset();
                submitAsyncRead(); // keepalive wait for next request
            } else {
                loop_->shutdown(channel_);
            }
        }
    }
}

void HttpData::handleReadComplete(int bytes_read) {
    if (closed_ || connectionState_ == H_DISCONNECTED) return;
    isReading_ = false;
    if (connectionState_ == H_DISCONNECTING) {
        inBuffer_.clear();
        return;
    }

    if (bytes_read < 0) {
        if (bytes_read == -EAGAIN) {
             channel_->getEvents() |= (EPOLLIN | EPOLLET);
             loop_->updatePoller(channel_);
             return;
        }
        if (bytes_read == -EINTR) {
             submitAsyncRead();
             return;
        }
        error_ = true;
        handleError(fd_, 400, "Bad Request");
        return;
    } else if (bytes_read == 0) {
        connectionState_ = H_DISCONNECTING;
    } else {
        inBuffer_.commitWrite(bytes_read);
        if (inBuffer_.readableBytes() >
            static_cast<int>(kMaxHeaderBytes + kMaxBodyBytes)) {
            error_ = true;
            handleError(fd_, 413, "Payload Too Large");
            return;
        }
    }

    if (state_ == STATE_WEBSOCKET) {
        handleWebSocketFrame();
        if (connectionState_ != H_DISCONNECTING) {
            submitAsyncRead();
        }
        return;
    }

    bool needsMoreData = false;
    while (!needsMoreData && state_ != STATE_FINISH && connectionState_ != H_DISCONNECTING) {
        if (state_ == STATE_PARSE_URI) {
            URIState flag = this->parseURI();
            if (flag == PARSE_URI_AGAIN) {
                needsMoreData = true;
            } else if (flag == PARSE_URI_ERROR) {
                inBuffer_.clear();
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                return;
            } else {
                state_ = STATE_PARSE_HEADERS;
            }
        }

        if (state_ == STATE_PARSE_HEADERS) {
            HeaderState flag = this->parseHeaders();
            if (flag == PARSE_HEADER_AGAIN) {
                needsMoreData = true;
            } else if (flag == PARSE_HEADER_ERROR) {
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                return;
            } else {
                if (method_ == METHOD_POST) {
                    state_ = STATE_RECV_BODY;
                } else {
                    state_ = STATE_ANALYSIS;
                }
            }
        }

        if (state_ == STATE_RECV_BODY) {
            if (!hasContentLength_) {
                error_ = true;
                handleError(fd_, 400, "Bad Request: Lack of content_length");
                return;
            }
            if (inBuffer_.readableBytes() < static_cast<int>(contentLength_)) {
                needsMoreData = true;
            } else {
                state_ = STATE_ANALYSIS;
            }
        }

        if (state_ == STATE_ANALYSIS) {
            AnalysisState flag = this->analysisRequest();
            if (flag == ANALYSIS_SUCCESS) {
                state_ = STATE_FINISH;
            } else if (flag == ANALYSIS_ERROR) {
                error_ = true;
                return;
            } else {
                needsMoreData = true;
            }
        }
    }

    if (needsMoreData && connectionState_ != H_DISCONNECTING) {
        submitAsyncRead();
    }
}

void HttpData::handleWriteComplete(int bytes_written) {
    if (closed_ || connectionState_ == H_DISCONNECTED) return;
    isWriting_ = false;
    if (bytes_written < 0) {
        if (bytes_written == -EAGAIN) {
             channel_->getEvents() |= (EPOLLOUT | EPOLLET);
             loop_->updatePoller(channel_);
             return;
        }
        if (bytes_written == -EINTR) {
             submitAsyncWrite();
             return;
        }
        error_ = true;
        handleClose();
        return;
    }
    
    outBuffer_.retrieve(bytes_written);
    if (outBuffer_.empty() && closeAfterWrite_) {
      handleClose();
      return;
    }
    submitAsyncWrite(); // Continue writing if there's more data
}

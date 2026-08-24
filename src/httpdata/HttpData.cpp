#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <iostream>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <set>
#include <mutex>
#include "HttpData.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Util.h"
#include "time.h"
#include"IoUring.h"
#include "SqlConnPool.h"
#include"json.hpp"
#include"ChatRoom.h"
#include"ChatManager.h"
#include "UserController.h"
#include "RoomController.h"
#include "CryptoUtil.h"
#include "MimeType.h"
using json = nlohmann::json;

const __uint32_t DEFAULT_EVENT = EPOLLIN | EPOLLET | EPOLLONESHOT;
const int DEFAULT_EXPIRED_TIME = 2000;              // ms
const int DEFAULT_KEEP_ALIVE_TIME = 5 * 60 * 1000;  // ms


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
    // 预分配 Header 空间，避免多次扩容
    std::string header;
    header.reserve(256); 
    
    header += "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    header += "Content-Type: " + type + "\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    header += "Connection: " + (keepAlive_ ? std::string("Keep-Alive") : std::string("close")) + "\r\n";
    if (keepAlive_) header += "Keep-Alive: timeout=20\r\n";
    header += "\r\n";

    // 2. 优化：不再进行 outBuffer_ = header + body 的大字符串拼接
    // 建议将 outBuffer_ 改为支持多块数据的结构，或者直接存入专门的响应队列
    outBuffer_.append(header);
    outBuffer_.append(body);
    submitAsyncWrite(); // 开始发送
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
        self->handleWriteInLoop(msg);
    });
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


HttpData::HttpData(EventLoop *loop, int connfd)
    : loop_(loop),
      channel_(new Channel(loop, connfd)),
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
  // keepAlive_ = false;
  if (timer_.lock()) {
    shared_ptr<TimerNode> my_timer(timer_.lock());
    my_timer->clearReq();
    timer_.reset();
  }
}

void HttpData::seperateTimer() {
  // cout << "seperateTimer" << endl;
  if (timer_.lock()) {
    shared_ptr<TimerNode> my_timer(timer_.lock());
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
            int content_length = -1;
            
            // --- [修复] 忽略大小写查找 Content-Length ---
            for(auto& h : headers_) {
                string k = h.first;
                std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                if(k == "content-length") {
                    content_length = stoi(h.second);
                    break;
                }
            }
            // ------------------------------------------

            if (content_length == -1) {
                // 如果找不到 Content-Length，报错
                error_ = true;
                handleError(fd_, 400, "Bad Request: Lack of argument (Content-length)");
                break;
            }

            // [关键] 检查数据有没有收全
            // 注意：inBuffer_ 此时包含了整个 body 的数据
            if (inBuffer_.readableBytes() < content_length) {
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
    std::string inStr = inBuffer_.peekAllAsString();
    while (inStr.size() >= 2) { // 至少要有头部 2 字节
        const unsigned char* data = (const unsigned char*)inStr.c_str();
        
        // --- 字节 1 ---
        bool fin = (data[0] & 0x80) != 0;
        int opcode = data[0] & 0x0F;
        
        // --- 字节 2 ---
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payloadLen = data[1] & 0x7F;
        
        int headLen = 2;
        
        // 处理扩展长度
        if (payloadLen == 126) {
            if (inStr.size() < 4) return; // 数据不够，等下次
            // 大端序转本机序 (简化处理，假设是 x86/ARM 小端)
            payloadLen = (data[2] << 8) | data[3];
            headLen = 4;
        } else if (payloadLen == 127) {
            if (inStr.size() < 10) return; // 数据不够
            // 简单处理，只取低位（一般 msg 不会这么大）
            payloadLen = 0; // 需要完整的 64 位转换逻辑
            headLen = 10; 
        }
        
        // 处理掩码 Key (客户端发来的必须有掩码)
        unsigned char maskingKey[4];
        if (masked) {
            if (inStr.size() < headLen + 4) return; // 数据不够
            for (int i = 0; i < 4; ++i) maskingKey[i] = data[headLen + i];
            headLen += 4;
        }
        
        // 检查 Payload 是否收齐
        if (inStr.size() < headLen + payloadLen) {
            return; // 数据不够，等待下一次 epoll 触发
        }
        
        // --- 开始解码数据 ---
        string payload;
        payload.resize(payloadLen);
        const unsigned char* rawPayload = data + headLen;
        
        for (size_t i = 0; i < payloadLen; ++i) {
            // XOR 解码：Data[i] ^ Mask[i % 4]
            payload[i] = rawPayload[i] ^ maskingKey[i % 4];
        }
        
        // --- 业务处理 ---
        if (opcode == 0x8) { // Close Frame
            connectionState_ = H_DISCONNECTING;
            inBuffer_.clear();
            return;
        } else if (opcode == 0x1) {
    try {
        // 1. 将解码后的 payload 转为 JSON 对象
        auto j = nlohmann::json::parse(payload);
        
        // 2. 提取业务指令
        std::string type = j.value("type", "unknown");

        if (type == "chat") {
            std::string roomId = j.at("roomId").get<std::string>();
            std::string content = j.at("content").get<std::string>();
            
            // 调用单例管理器进行房间广播
            ChatManager::getInstance().broadcastToRoom(roomId,content,this->fd_);
        } 
        else if (type == "join") {
            std::string roomId = j.at("roomId").get<std::string>();
            ChatManager::getInstance().joinRoom(roomId, shared_from_this());
            // 可选：回发一个 JSON 确认包
        }
        
    } catch (const std::exception& e) {
        // 重要：如果不是 JSON 格式，捕获异常防止崩溃
        LOG << "非法的业务报文格式: " << e.what();
    }
  }
        // 移除已处理的一帧数据
        inBuffer_.retrieve(headLen + payloadLen);
        inStr = inBuffer_.peekAllAsString(); // refresh the peeked string loop
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
        if (!keepAlive_ && connectionState_ == H_DISCONNECTING) {
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
    loop_->runInLoop(bind(&HttpData::handleClose, shared_from_this()));
  }
}

URIState HttpData::parseURI() {
    std::string str = inBuffer_.peekAllAsString();
    size_t pos = str.find("\r\n");
    if (pos == std::string::npos)
        return PARSE_URI_AGAIN;

    std::string request_line = str.substr(0, pos);
    inBuffer_.retrieve(pos + 2); // 吃掉 \r\n

    // method
    size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos)
        return PARSE_URI_ERROR;

    std::string method = request_line.substr(0, method_end);
    if (method == "GET") method_ = METHOD_GET;
    else if (method == "POST") method_ = METHOD_POST;
    else if (method == "HEAD") method_ = METHOD_HEAD;
    else return PARSE_URI_ERROR;

    // uri
    size_t uri_end = request_line.find(' ', method_end + 1);
    if (uri_end == std::string::npos)
        return PARSE_URI_ERROR;

    uri_ = request_line.substr(method_end + 1, uri_end - method_end - 1);

    // 去掉 query
    size_t qpos = uri_.find('?');
    if (qpos != std::string::npos) {
        query_ = uri_.substr(qpos + 1);
        uri_ = uri_.substr(0, qpos);
    }

    // 文件名
    if (uri_ == "/")
        fileName_ = "index.html";
    else
        fileName_ = uri_.substr(1);

    // version
    std::string version = request_line.substr(uri_end + 1);
    if (version == "HTTP/1.1") HTTPVersion_ = HTTP_11;
    else if (version == "HTTP/1.0") HTTPVersion_ = HTTP_10;
    else return PARSE_URI_ERROR;
    /*
    std::cout<< "[parseURI] method=" << method_
    << " uri=" << uri_
    << " fileName=" << fileName_
    << " inBuffer=[" << inBuffer_ << "]"<<std::endl;
    */
    return PARSE_URI_SUCCESS;
}


HeaderState HttpData::parseHeaders() {
    std::string str = inBuffer_.peekAllAsString();
    size_t pos = str.find("\r\n\r\n");
    if (pos == std::string::npos) return PARSE_HEADER_AGAIN;//string::npos未找到

    std::string header_str = str.substr(0, pos);
    inBuffer_.retrieve(pos + 4); 

    size_t start = 0;
    while (start < header_str.size()) {
        size_t end = header_str.find("\r\n", start);
        if (end == std::string::npos) break;

        std::string line = header_str.substr(start, end - start);
        start = end + 2;

        // 重点：去掉行首可能存在的多余换行或空格
        while(!line.empty() && (line[0] == '\n' || line[0] == '\r')) 
            line.erase(line.begin());

        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // 去掉 Value 的空格
        size_t v_start = value.find_first_not_of(" \t");
        size_t v_end = value.find_last_not_of(" \t");
        if (v_start != std::string::npos)
            headers_[key] = value.substr(v_start, v_end - v_start + 1);
    }

    // 根据 Connection 头和 HTTP 版本设置 keepAlive_
    // HTTP/1.1 默认长连接；HTTP/1.0 默认短连接
    auto it = headers_.find("Connection");
    if (it != headers_.end()) {
        std::string conn = it->second;
        std::transform(conn.begin(), conn.end(), conn.begin(), ::tolower);
        keepAlive_ = (conn.find("keep-alive") != std::string::npos);
    } else {
        keepAlive_ = (HTTPVersion_ == HTTP_11);
    }

    return PARSE_HEADER_SUCCESS;
}

AnalysisState HttpData::handleWebSocketHandshake() {

    // 1. 必须是 WebSocket 升级请求
    if (headers_.find("Upgrade") == headers_.end() ||
        headers_.find("Sec-WebSocket-Key") == headers_.end()) {
        handleError(fd_, 400, "Bad WebSocket Handshake");
        return ANALYSIS_ERROR;
    }

    // 2. 检查 Upgrade 字段
    std::string upgrade_val = headers_["Upgrade"];
    std::transform(upgrade_val.begin(), upgrade_val.end(), upgrade_val.begin(), ::tolower);
    if (upgrade_val != "websocket") {
        handleError(fd_, 400, "Not WebSocket");
        return ANALYSIS_ERROR;
    }

    // 3. JWT 鉴权（从 URL query 中取 token）
    std::string token = getQueryParam("token");
    if (token.empty() || !CryptoUtil::verifyJWT(token)) {
        handleError(fd_, 401, "Unauthorized");
        return ANALYSIS_ERROR;
    }

    // 4. 提取用户名
    std::string username_ = CryptoUtil::extractUsername(token);

    // LOG << "WebSocket 握手成功，用户: " << username_ << " fd=" << fd_; // removed: locks AsyncLogging mutex

    // 5. 计算 Accept Key
    std::string clientKey = headers_["Sec-WebSocket-Key"];
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

std::string HttpData::getQueryParam(const std::string& key) {
    std::string target = key + "=";
    size_t pos = query_.find(target);
    if (pos == std::string::npos) return "";

    size_t start = pos + target.size();
    size_t end = query_.find('&', start);
    if (end == std::string::npos) end = query_.size();

    return query_.substr(start, end - start);
}

AnalysisState HttpData::analysisRequest() {

    // ==========================================================
    // 1. 处理注册接口
    // ==========================================================
    if (method_ == METHOD_POST && uri_ == "/register") {
        UserController::handleRegister(shared_from_this());
        return ANALYSIS_SUCCESS;
    }

    if (uri_ == "/ping") {
    sendResponse(200, "text/plain", "OK");
    return ANALYSIS_SUCCESS;
}

    // ==========================================================
    // 2. 处理登录接口
    // ==========================================================
    if (method_ == METHOD_POST && uri_ == "/login") {
        UserController::handleLogin(shared_from_this());
        return ANALYSIS_SUCCESS;
    }

    // ==========================================================
    // 3. WebSocket 握手（必须在普通 GET 之前）
    // ==========================================================
    if (method_ == METHOD_GET && uri_ == "/ws") {
        return handleWebSocketHandshake();
    }

    // ==========================================================
    // 4. 普通 HTTP GET（静态文件）
    // ==========================================================
    if (method_ == METHOD_GET || method_ == METHOD_HEAD) {
        return handleStaticFile();
    }

    if (path_ == "/room/create" && method_ == METHOD_POST) {
        RoomController::handleCreateRoom(shared_from_this());
        return ANALYSIS_SUCCESS;
    } 
    if (path_ == "/room/list" && method_ == METHOD_GET) {
        RoomController::handleGetRoomList(shared_from_this());
        return ANALYSIS_SUCCESS;
    }

    //其他情况
    handleError(fd_, 405, "Method Not Allowed");
    return ANALYSIS_ERROR;
}


AnalysisState HttpData::handleStaticFile() {
    // 1. 修正路径拼接逻辑
    if (fileName_.empty() || fileName_ == "/") {
        fileName_ = "index.html";
    }
    // 确保 path 结构正确：./wwwroot/index.html
    std::string path = "./wwwroot/" + fileName_;

    struct stat sbuf;
    // 2. 检查文件状态
    if (stat(path.c_str(), &sbuf) < 0 || S_ISDIR(sbuf.st_mode)) {
        // std::cout removed: blocking stdout flush on every 404 kills worker threads
        handleError(fd_, 404, "Not Found");
        // --- 重点修改：返回 SUCCESS，允许服务器把 404 页面发给浏览器 ---
        return ANALYSIS_SUCCESS; 
    }

    // 3. 获取 MIME 类型
    std::string filetype = "text/plain";
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
        filetype = MimeType::getMime(path.substr(dot));

    // 4. 构造响应头
    std::string header;
    header += "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: " + filetype + "\r\n";
    header += "Content-Length: " + std::to_string(sbuf.st_size) + "\r\n";
    header += "Connection: " + (keepAlive_ ? std::string("Keep-Alive") : std::string("Close")) + "\r\n";
    header += "\r\n";

    outBuffer_.append(header);

    // 5. 打开文件准备发送
    int src_fd = open(path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        handleError(fd_, 404, "Not Found");
        return ANALYSIS_SUCCESS;
    }

    // 立刻把响应头发送出去（如果网卡缓存满了报 EAGAIN 丢弃也不影响测试大局）
    int savedErrno = 0;
    ssize_t head_n = outBuffer_.writeFd(fd_, &savedErrno);
    if (head_n == -1) head_n = 0;

    // 使用 sendfile 把文件体在内核态直接塞给网卡！真正的零拷贝！
    off_t offset = 0;
    sendfile(fd_, src_fd, &offset, sbuf.st_size);

    close(src_fd);

    return ANALYSIS_SUCCESS;
}

void HttpData::handleError(int fd, int err_num, string short_msg) {
  short_msg = " " + short_msg;
  char send_buff[4096];
  string body_buff, header_buff;
  body_buff += "<html><title>哎~出错了</title>";
  body_buff += "<body bgcolor=\"ffffff\">";
  body_buff += to_string(err_num) + short_msg;
  body_buff += "<hr><em> LinYa's Web Server</em>\n</body></html>";

  header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
  header_buff += "Content-Type: text/html\r\n";
  header_buff += "Connection: Close\r\n";
  header_buff += "Content-Length: " + to_string(body_buff.size()) + "\r\n";
  header_buff += "Server: LinYa's Web Server\r\n";
  ;
  header_buff += "\r\n";
  // 错误处理不考虑writen不完的情况
  sprintf(send_buff, "%s", header_buff.c_str());
  writen(fd, send_buff, strlen(send_buff));
  sprintf(send_buff, "%s", body_buff.c_str());
  writen(fd, send_buff, strlen(send_buff));
}

void HttpData::handleClose() {
  if (closed_) return;
  closed_ = true;
  connectionState_ = H_DISCONNECTED;

  // Keep the object alive while detaching the timer. TimerNode owns a shared
  // pointer to HttpData and clearReq() can otherwise release the final owner.
  const shared_ptr<HttpData> guard(shared_from_this());
  if (channel_) {
    if (loop_) {
      loop_->removeFromPoller(channel_);
    } else {
      channel_->deactivate();
    }
    channel_->clearHolder();
  }
  seperateTimer();
  for (const auto& [id, room] : rooms_) {
    if (room) {
      room->leave(fd_);
    }
  }
  rooms_.clear();
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
            int content_length = -1;
            if (headers_.find("Content-Length") != headers_.end()) {
                content_length = std::stoi(headers_["Content-Length"]);
            } else {
                error_ = true;
                handleError(fd_, 400, "Bad Request: Lack of content_length");
                return;
            }
            if (inBuffer_.readableBytes() < content_length) {
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
        return;
    }
    
    outBuffer_.retrieve(bytes_written);
    submitAsyncWrite(); // Continue writing if there's more data
}

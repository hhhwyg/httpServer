// @Author Lin Ya
// @Email xxbbb@vip.qq.com
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
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
#include"Epoll.h"
#include "SqlConnPool.h"
#include"json.hpp"
#include"ChatRoom.h"
#include"ChatManager.h"
using json = nlohmann::json;

pthread_once_t MimeType::once_control = PTHREAD_ONCE_INIT;
std::unordered_map<std::string, std::string> MimeType::mime;
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
 void HttpData::handleRegister() {
    json root;
    try {
        root = json::parse(inBuffer_);
    } catch (...) {
        sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid JSON\"}");
        return;
    }

    std::string username = root["username"];
    std::string password = root["password"];

    if (!registerUser(username, password)) {
        sendResponse(200, "application/json", "{\"ok\":false,\"msg\":\"User exists\"}");
        return;
    }

    json resp;
    resp["ok"] = true;
    sendResponse(200, "application/json", resp.dump());
}


void HttpData::handleLogin() {
    json root;
    try {
        root = json::parse(inBuffer_);
    } catch (...) {
        sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid JSON\"}");
        return;
    }

    if (!root.contains("username") || !root.contains("password")) {
        sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Missing fields\"}");
        return;
    }

    std::string username = root["username"];
    std::string password = root["password"];

    if (!checkLogin(username, password)) {
        sendResponse(200, "application/json", "{\"ok\":false,\"msg\":\"Wrong username or password\"}");
        return;
    }

    std::string token = generateJWT(username);

    json resp;
    resp["ok"] = true;
    resp["token"] = token;

    sendResponse(200, "application/json", resp.dump());
}

void HttpData::handleCreateRoom() {
    json root = json::parse(inBuffer_);
    std::string roomName = root["name"];
    
    // 在 ChatManager 中创建并获取新 ID
    auto newRoom = ChatManager::getInstance().getOrCreateRoom(roomName);
    
    json resp;
    resp["ok"] = true;
    resp["roomId"] = newRoom->roomId_;
    sendResponse(200, "application/json", resp.dump());
}


void HttpData::handleGetRoomList() {
    // 从 ChatManager 获取所有房间
    auto rooms = ChatManager::getInstance().getAllRooms(); 
    
    json resp;
    resp["ok"] = true;
    resp["rooms"] = json::array();
    for (auto& room : rooms) {
        resp["rooms"].push_back({{"id", room->roomId_}, {"name", room->name_}});
    }
    // 发送 JSON 响应
    sendResponse(200, "application/json", resp.dump());
}

std::string HttpData::generateJWT(const std::string& username) {
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string payload = "{\"username\":\"" + username + "\"}";

    std::string encodedHeader = base64UrlEncode(header);
    std::string encodedPayload = base64UrlEncode(payload);

    std::string toSign = encodedHeader + "." + encodedPayload;
    std::string signature = base64UrlEncode(hmacSha256(toSign, "my_secret_key"));

    return encodedHeader + "." + encodedPayload + "." + signature;
}

std::string HttpData::hmacSha256(const std::string& data, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.data(), key.size(),
         (unsigned char*)data.data(), data.size(),
         hash, &len);

    return std::string((char*)hash, len);
}


// Base64URL 编码（JWT 必须用 URL-safe Base64）
std::string HttpData::base64UrlEncode(const std::string& input) {
    // 1. Base64 编码
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // 不要换行
    BIO_write(b64, input.data(), input.size());
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string encoded(bptr->data, bptr->length);
    BIO_free_all(b64);

    // 2. 转换为 URL-safe Base64
    for (auto &c : encoded) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    // 3. 去掉 '='
    encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end());

    return encoded;
}



bool HttpData::verifyJWT(const std::string& token) {
    size_t p1 = token.find('.');
    size_t p2 = token.find('.', p1 + 1);
    if (p1 == string::npos || p2 == string::npos) return false;

    std::string header = token.substr(0, p1);
    std::string payload = token.substr(p1 + 1, p2 - p1 - 1);
    std::string signature = token.substr(p2 + 1);

    std::string toSign = header + "." + payload;
    std::string expected = base64UrlEncode(hmacSha256(toSign, "my_secret_key"));

    return expected == signature;
}


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
    outBuffer_+=header;
    outBuffer_+=body;
    handleWrite(); // 开始发送
}




void HttpData::handleWriteInLoop(const std::string& msg) {
    if (error_ || connectionState_ == H_DISCONNECTED) return;

    // 统一追加到缓冲区末尾
    outBuffer_ += msg; 

    // 尝试立即发送
    handleWrite();
}

void HttpData::sendMsg(const std::string& msg) {
    // 1. 延长对象生命周期，确保异步任务执行时对象不过期
    auto self = shared_from_this(); 
    
    // 2. 将任务丢进所属 Loop 的队列，确保线程安全
    loop_->runInLoop([self, msg]() {
        self->handleWriteInLoop(msg);
    });
}



bool HttpData::registerUser(const std::string& username,
                            const std::string& password) {
    MYSQL* sql = nullptr;
    SqlConnRAII conn(&sql, SqlConnPool::Instance());
    if(!sql) return false;

    // 1. 先检查是否已存在
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id FROM user WHERE username='%s' LIMIT 1",
             username.c_str());
    if(mysql_query(sql, query) != 0) {
        return false;
    }
    MYSQL_RES* res = mysql_store_result(sql);
    bool exists = (res && mysql_fetch_row(res));
    if(res) mysql_free_result(res);
    if(exists) return false;   // 已存在，注册失败

    // 2. 插入新用户（这里没做密码加密，真实项目建议做 hash）
    char insert[256];
    snprintf(insert, sizeof(insert),
             "INSERT INTO user(username, passwd) VALUES('%s', '%s')",
             username.c_str(), password.c_str());
    if(mysql_query(sql, insert) != 0) {
        return false;
    }
    return true;
}


// 处理登录的函数
bool HttpData::checkLogin(const string& username, const string& password) {
    // 1. 定义一个 MySQL 指针
    MYSQL* sql = nullptr;
    
    // 2. 使用 RAII 获取连接 (核心步骤！)
    // 出了这个花括号作用域，连接就会自动归还给池子，不用怕忘记 FreeConn
    SqlConnRAII conn(&sql, SqlConnPool::Instance());
    assert(sql);

    // 3. 执行查询
    char order[256] = { 0 };
    // 注意：实际项目要防 SQL 注入，这里为了演示直接拼串
    snprintf(order, 256, "SELECT username, passwd FROM user WHERE username='%s' LIMIT 1", username.c_str());

    if(mysql_query(sql, order)) {
        // mysql_query 返回 0 表示成功，非 0 表示失败
        // LOG << "SELECT error:" << mysql_error(sql); 
        return false;
    }

    // 4. 获取结果
    MYSQL_RES *res = mysql_store_result(sql);
    if(!res) return false;

    // 5. 验证密码
    bool flag = false;
    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        string dbPassword(row[1]);
        if(password == dbPassword) {
            flag = true;
        }
    }
    mysql_free_result(res); // 记得释放结果集内存
    return flag;
}

 ProcessState HttpData::getRequestStatus(){
  return state_;
 }
  string& HttpData::getOutBuffer(){
    return outBuffer_;
  }
  void HttpData::ctlHandleWrite(){
    this->handleWrite();
  }


// 辅助函数：计算 WebSocket 握手 Key
std::string computeAcceptKey(std::string contextKey) {
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input = contextKey + magic;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)input.c_str(), input.length(), hash);

    // Base64 编码 (OpenSSL 方式)
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // 不换行
    BIO_write(b64, hash, SHA_DIGEST_LENGTH);
    BIO_flush(b64);
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string output(bptr->data, bptr->length);
    BIO_free_all(b64);
    return output;
}


void MimeType::init() {
  mime[".html"] = "text/html";
  mime[".avi"] = "video/x-msvideo";
  mime[".bmp"] = "image/bmp";
  mime[".c"] = "text/plain";
  mime[".css"] = "text/css";              
  mime[".js"] = "application/javascript"; 
  mime[".json"] = "application/json";     
  mime[".doc"] = "application/msword";
  mime[".gif"] = "image/gif";
  mime[".gz"] = "application/x-gzip";
  mime[".htm"] = "text/html";
  mime[".icg"] = "image/png";
  mime[".txo"] = "image/x-icon";
  mime[".jpg"] = "image/jpeg";
  mime[".pnt"] = "text/plain";
  mime[".mp3"] = "audio/mp3";
  mime["default"] = "text/html";
}

std::string MimeType::getMime(const std::string &suffix) {
  pthread_once(&once_control, MimeType::init);
  if (mime.find(suffix) == mime.end())
    return mime["default"];
  else
    return mime[suffix];
}

HttpData::HttpData(EventLoop *loop, int connfd)
    : loop_(loop),
      channel_(new Channel(loop, connfd)),
      fd_(connfd),
      error_(false),
      closed_(false),
      connectionState_(H_CONNECTED),
      method_(METHOD_GET),
      HTTPVersion_(HTTP_11),
      nowReadPos_(0),
      state_(STATE_PARSE_URI),
      hState_(H_START),
      keepAlive_(false) {
  // loop_->queueInLoop(bind(&HttpData::setHandlers, this));
  //channel_->setReadHandler(bind(&HttpData::handleRead, this));
  //channel_->setWriteHandler(bind(&HttpData::handleWrite, this));
  //channel_->setConnHandler(bind(&HttpData::handleConn, this));
}


void HttpData::init() {
  // 使用 shared_from_this() 而不是 this
  auto self = shared_from_this();
  channel_->setReadHandler(bind(&HttpData::handleRead, self));
  channel_->setWriteHandler(bind(&HttpData::handleWrite, self));
  channel_->setConnHandler(bind(&HttpData::handleConn, self));
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
        bool zero = false;
        int read_num = readn(fd_, inBuffer_, zero);
        /*
        std::cout << "[handleRead] read_num=" << read_num 
          << " zero=" << zero 
          << " inBuffer=[" << inBuffer_ << "]" << std::endl;
          */
        LOG << "Request: " << read_num << " bytes"; 

        if (connectionState_ == H_DISCONNECTING) {
            inBuffer_.clear();
            break;
        }

        if (read_num < 0) {
            perror("readn");
            error_ = true;
            handleError(fd_, 400, "Bad Request");
            break;
        }
        else if (zero) {
            connectionState_ = H_DISCONNECTING;
            if (read_num == 0) break;
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
            // 注意：inBuffer_ 此时包含了整个 body 的数据（因为头部已经被 parseHeaders 移除了）
            if (static_cast<int>(inBuffer_.size()) < content_length) {
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
    if (outBuffer_.size() > 0) {
      handleWrite();
      // events_ |= EPOLLOUT;
    }
    // error_ may change
    if (!error_ && state_ == STATE_FINISH) {
      this->reset();
      if (inBuffer_.size() > 0) {
        if (connectionState_ != H_DISCONNECTING) handleRead();
      }

      // if ((keepAlive_ || inBuffer_.size() > 0) && connectionState_ ==
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
    // inBuffer_ 此时包含的是 TCP 流中的二进制数据
    // 需要循环处理，因为可能一次发来多条消息（粘包），或者一条消息分多次发（半包）
    
    while (inBuffer_.size() >= 2) { // 至少要有头部 2 字节
        const unsigned char* data = (const unsigned char*)inBuffer_.c_str();
        
        // --- 字节 1 ---
        bool fin = (data[0] & 0x80) != 0;
        int opcode = data[0] & 0x0F;
        
        // --- 字节 2 ---
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payloadLen = data[1] & 0x7F;
        
        int headLen = 2;
        
        // 处理扩展长度
        if (payloadLen == 126) {
            if (inBuffer_.size() < 4) return; // 数据不够，等下次
            // 大端序转本机序 (简化处理，假设是 x86/ARM 小端)
            payloadLen = (data[2] << 8) | data[3];
            headLen = 4;
        } else if (payloadLen == 127) {
            if (inBuffer_.size() < 10) return; // 数据不够
            // 简单处理，只取低位（一般 msg 不会这么大）
            payloadLen = 0; // 需要完整的 64 位转换逻辑
            headLen = 10; 
        }
        
        // 处理掩码 Key (客户端发来的必须有掩码)
        unsigned char maskingKey[4];
        if (masked) {
            if (inBuffer_.size() < headLen + 4) return; // 数据不够
            for (int i = 0; i < 4; ++i) maskingKey[i] = data[headLen + i];
            headLen += 4;
        }
        
        // 检查 Payload 是否收齐
        if (inBuffer_.size() < headLen + payloadLen) {
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
        inBuffer_ = inBuffer_.substr(headLen + payloadLen);
    }
}


void HttpData::handleWrite() {
    if (error_ || connectionState_ == H_DISCONNECTED) return;
    if (outBuffer_.empty()) return;

    // 1. 调用系统写函数
    ssize_t n = write(fd_, outBuffer_.c_str(), outBuffer_.size());

    if (n > 0) {
        // 2. 发送了多少，就从缓冲区头部删除多少
        // 这样下一次发送依然从 outBuffer_.c_str() (偏移量0) 开始
        outBuffer_.erase(0, n);
    } else if (n < 0 && errno != EAGAIN) {
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


    std::string &str = inBuffer_;

    size_t pos = str.find("\r\n");
    if (pos == std::string::npos)
        return PARSE_URI_AGAIN;

    std::string request_line = str.substr(0, pos);
    str = str.substr(pos + 2); // 吃掉 \r\n

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
    std::string &str = inBuffer_;
    size_t pos = str.find("\r\n\r\n");
    if (pos == std::string::npos) return PARSE_HEADER_AGAIN;

    std::string header_str = str.substr(0, pos);
    str = str.substr(pos + 4); 

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
    
    //std::cout << "[parseHeaders] SUCCESS: " << headers_.size() << " headers parsed." << std::endl;
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
    if (token.empty() || !verifyJWT(token)) {
        handleError(fd_, 401, "Unauthorized");
        return ANALYSIS_ERROR;
    }

    // 4. 提取用户名
    std::string username_ = extractUsername(token);

    LOG << "WebSocket 握手成功，用户: " << username_ << " fd=" << fd_;

    // 5. 计算 Accept Key
    std::string clientKey = headers_["Sec-WebSocket-Key"];
    std::string acceptKey = computeAcceptKey(clientKey);

    // 6. 构造握手响应
    std::string header;
    header += "HTTP/1.1 101 Switching Protocols\r\n";
    header += "Upgrade: websocket\r\n";
    header += "Connection: Upgrade\r\n";
    header += "Sec-WebSocket-Accept: " + acceptKey + "\r\n";
    header += "\r\n";

    outBuffer_ += header;

    // 7. 切换状态机
    state_ = STATE_WEBSOCKET;
    keepAlive_ = true;

    // 8. 发送握手响应
    handleWrite();

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

std::string HttpData::extractUsername(const std::string& token) {
    size_t p1 = token.find('.');
    size_t p2 = token.find('.', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos)
        return "";

    std::string payload = token.substr(p1 + 1, p2 - p1 - 1);

    // Base64URL 解码
    std::string jsonStr = base64UrlDecode(payload);

    // 解析 JSON
    try {
        auto j = json::parse(jsonStr);
        return j.value("username", "");
    } catch (...) {
        return "";
    }
}

std::string HttpData::base64UrlDecode(const std::string& input) {
    std::string s = input;

    // URL-safe → normal Base64
    for (auto &c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }

    // 补齐 '='
    while (s.size() % 4 != 0) s += '=';

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new_mem_buf(s.data(), s.size());
    bmem = BIO_push(b64, bmem);

    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    char buffer[2048];
    int len = BIO_read(bmem, buffer, sizeof(buffer));
    BIO_free_all(bmem);

    return std::string(buffer, len);
}


AnalysisState HttpData::analysisRequest() {

    // ==========================================================
    // 1. 处理注册接口
    // ==========================================================
    if (method_ == METHOD_POST && uri_ == "/register") {
        handleRegister();
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
        handleLogin();
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
        handleCreateRoom();
        return ANALYSIS_SUCCESS;
    } 
    if (path_ == "/room/list" && method_ == METHOD_GET) {
        handleGetRoomList();
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
        std::cout << "[handleStaticFile] 404 Not Found: " << path << std::endl;
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

    outBuffer_ += header;

    // 5. 安全地读取文件 (mmap 必须检查返回值)
    int src_fd = open(path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        handleError(fd_, 404, "Not Found");
        return ANALYSIS_SUCCESS;
    }
    
    void* mmapRet = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    close(src_fd);

    if (mmapRet == MAP_FAILED) { // 检查 mmap 是否成功
        perror("mmap");
        outBuffer_.clear(); // 清除已有的 Header
        handleError(fd_, 500, "Internal Server Error");
        return ANALYSIS_SUCCESS;
    }

    outBuffer_.append((char*)mmapRet, sbuf.st_size);
    munmap(mmapRet, sbuf.st_size);

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
  if(closed_) return;
  closed_=true;
  connectionState_ = H_DISCONNECTED;
  shared_ptr<HttpData> guard(shared_from_this());
  //通过定义一个局部变量 guard，人为地将该对象的引用计数 +1。只要 guard 还在这个函数的作用域内，对象就绝对不会被析构。
  loop_->removeFromPoller(channel_);
  for (const auto& [id, room] : rooms_) {
        if (room) {
            room->leave(fd_); 
        }
    }
    rooms_.clear(); // 彻底断开与所有房间的联系
}

void HttpData::newEvent() {
  channel_->setEvents(DEFAULT_EVENT);
  loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
  
}

import os

# Modify HttpData.h
with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.h", "r") as f:
    h_content = f.read()

h_content = h_content.replace(
    "void handleWebSocketHandshake();",
    "AnalysisState handleWebSocketHandshake();"
)

h_content = h_content.replace(
    "void handleRead();",
    "void handleRead();\n  void handleReadComplete(int bytes_read);\n  void handleWriteComplete(int bytes_written);\n  void submitAsyncRead();\n  void submitAsyncWrite();\n  bool isWriting_;"
)

with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.h", "w") as f:
    f.write(h_content)


# Create a new HttpData.cpp replacing handleRead and handleWrite
cpp_patch = """
void HttpData::submitAsyncRead() {
    char* buf = nullptr;
    int len = 0;
    inBuffer_.getWritableChunkInfo(&buf, &len);
    if (buf && len > 0) {
        loop_->submitRead(channel_, buf, len);
    }
}

void HttpData::submitAsyncWrite() {
    if (isWriting_) return;
    
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
    if (connectionState_ == H_DISCONNECTING) {
        inBuffer_.clear();
        return;
    }

    if (bytes_read < 0) {
        if (bytes_read == -EAGAIN || bytes_read == -EINTR) {
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
    isWriting_ = false;
    if (bytes_written < 0) {
        if (bytes_written == -EAGAIN || bytes_written == -EINTR) {
             submitAsyncWrite();
             return;
        }
        error_ = true;
        return;
    }
    
    outBuffer_.retrieve(bytes_written);
    submitAsyncWrite(); // Continue writing if there's more data
}
"""

with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.cpp", "r") as f:
    cpp_content = f.read()

# Replace ctlHandleWrite and handleWrite calls
cpp_content = cpp_content.replace("this->handleWrite();", "submitAsyncWrite();")
cpp_content = cpp_content.replace("handleWrite();", "submitAsyncWrite();")

# Replace the constructor initialization list
cpp_content = cpp_content.replace(
    "keepAlive_(false) {",
    "keepAlive_(false),\n      isWriting_(false) {"
)

# Update init() to register the async handlers
init_old = """void HttpData::init() {
  // 使用 shared_from_this() 而不是 this
  auto self = shared_from_this();
  channel_->setReadHandler(bind(&HttpData::handleRead, self));
  channel_->setWriteHandler(bind(&HttpData::handleWrite, self));
  channel_->setConnHandler(bind(&HttpData::handleConn, self));
}"""

init_new = """void HttpData::init() {
  auto self = shared_from_this();
  channel_->setReadHandler(bind(&HttpData::handleRead, self));
  channel_->setWriteHandler(bind(&HttpData::handleWrite, self));
  channel_->setConnHandler(bind(&HttpData::handleConn, self));
  channel_->setAsyncReadHandler(bind(&HttpData::handleReadComplete, self, std::placeholders::_1));
  channel_->setAsyncWriteHandler(bind(&HttpData::handleWriteComplete, self, std::placeholders::_1));
}"""

cpp_content = cpp_content.replace(init_old, init_new)

# Modify newEvent
new_event_old = """void HttpData::newEvent() {
  channel_->setEvents(DEFAULT_EVENT);
  loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
}"""

new_event_new = """void HttpData::newEvent() {
  channel_->setEvents(0); // For io_uring proactor, we don't need POLL_ADD for HttpData
  loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
  submitAsyncRead(); // proactively start reading!
}"""

cpp_content = cpp_content.replace(new_event_old, new_event_new)


# Remove the old handleRead implementation
import re
# We'll just replace the whole handleRead body.
match_handle_read = re.search(r"void HttpData::handleRead\(\) \{.*?\}\nvoid HttpData::handleWrite\(\) \{.*?\}\nvoid HttpData::handleConn\(\) \{", cpp_content, re.DOTALL)
if match_handle_read:
    old_read_write = match_handle_read.group(0)
    # remove the last line (handleConn signature)
    old_read_write = old_read_write[:old_read_write.rfind("void HttpData::handleConn() {")]
    
    new_read_write = """void HttpData::handleRead() {
    // Deprecated in Proactor mode
}

void HttpData::handleWrite() {
    // Deprecated in Proactor mode
}
""" + cpp_patch + "\n"

    cpp_content = cpp_content.replace(old_read_write, new_read_write)


with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.cpp", "w") as f:
    f.write(cpp_content)

print("HttpData rewrite done.")

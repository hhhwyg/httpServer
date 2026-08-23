import sys

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

with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.cpp", "a") as f:
    f.write(cpp_patch)

print("Done appending to HttpData.cpp")

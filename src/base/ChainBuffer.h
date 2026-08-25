#pragma once
#include "MemoryPool.h"
#include <vector>
#include <string>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>

struct BufferChunk {
    char* data;
    int readPos;
    int writePos;
    
    BufferChunk() {
        data = MemoryPool::Instance().allocateChunk();
        readPos = 0;
        writePos = 0;
    }
    
    ~BufferChunk() {
        MemoryPool::Instance().deallocateChunk(data);
    }
    
    int readableBytes() const { return writePos - readPos; }
    int writableBytes() const { return CHUNK_SIZE - writePos; }
};

class ChainBuffer {
public:
    ChainBuffer() : totalBytes_(0) {}
    ~ChainBuffer() {
        for(auto chunk : chunks_) {
            delete chunk;
        }
    }
    
    int readableBytes() const { return totalBytes_; }
    bool empty() const { return totalBytes_ == 0; }
    
    void append(const char* data, int len) {
        int remaining = len;
        const char* ptr = data;
        
        while(remaining > 0) {
            if (chunks_.empty() || chunks_.back()->writableBytes() == 0) {
                chunks_.push_back(new BufferChunk());
            }
            
            BufferChunk* last = chunks_.back();
            int writable = last->writableBytes();
            int copyLen = std::min(remaining, writable);
            
            memcpy(last->data + last->writePos, ptr, copyLen);
            last->writePos += copyLen;
            remaining -= copyLen;
            ptr += copyLen;
            totalBytes_ += copyLen;
        }
    }
    
    void append(const std::string& str) {
        append(str.data(), str.size());
    }
    
    // 从Buffer中取走一段数据并丢弃
    void retrieve(int len) {
        if (len > totalBytes_) len = totalBytes_;
        int remaining = len;
        
        while (remaining > 0 && !chunks_.empty()) {
            BufferChunk* first = chunks_.front();
            int readable = first->readableBytes();
            
            if (remaining >= readable) {
                remaining -= readable;
                totalBytes_ -= readable;
                delete first;
                chunks_.erase(chunks_.begin());
            } else {
                first->readPos += remaining;
                totalBytes_ -= remaining;
                remaining = 0;
            }
        }
    }
    
    // 清空缓冲区
    void clear() {
        for(auto chunk : chunks_) {
            delete chunk;
        }
        chunks_.clear();
        totalBytes_ = 0;
    }
    
    // 获取全部内容的 std::string 副本
    std::string readAllAsString() {
        std::string res;
        res.reserve(totalBytes_);
        for(auto chunk : chunks_) {
            res.append(chunk->data + chunk->readPos, chunk->readableBytes());
        }
        retrieve(totalBytes_);
        return res;
    }

    // 仅仅查看内容而不删除
    std::string peekAllAsString() const {
        std::string res;
        res.reserve(totalBytes_);
        for(auto chunk : chunks_) {
            res.append(chunk->data + chunk->readPos, chunk->readableBytes());
        }
        return res;
    }

    ssize_t readFd(int fd, int* savedErrno) {
        char extrabuf[65536]; // 栈上分配64K
        
        if (chunks_.empty() || chunks_.back()->writableBytes() == 0) {
            chunks_.push_back(new BufferChunk());
        }
        
        BufferChunk* last = chunks_.back();
        int writable = last->writableBytes();
        
        struct iovec vec[2];
        vec[0].iov_base = last->data + last->writePos;
        vec[0].iov_len = writable;
        vec[1].iov_base = extrabuf;
        vec[1].iov_len = sizeof(extrabuf);
        
        const int iovcnt =
            (writable < static_cast<int>(sizeof(extrabuf))) ? 2 : 1;
        const ssize_t n = readv(fd, vec, iovcnt);
        
        if (n < 0) {
            *savedErrno = errno;
        } else if (n <= writable) {
            last->writePos += n;
            totalBytes_ += n;
        } else {
            last->writePos += writable;
            totalBytes_ += writable;
            append(extrabuf, n - writable);
        }
        return n;
    }

    // 给 io_uring 提供连续内存进行异步读
    void getWritableChunkInfo(char** buf, int* len) {
        if (chunks_.empty() || chunks_.back()->writableBytes() == 0) {
            chunks_.push_back(new BufferChunk());
        }
        BufferChunk* last = chunks_.back();
        *buf = last->data + last->writePos;
        *len = last->writableBytes();
    }

    void commitWrite(int bytes) {
        if (bytes <= 0) return;
        BufferChunk* last = chunks_.back();
        last->writePos += bytes;
        totalBytes_ += bytes;
    }
    
    // 给 io_uring 提供连续内存进行异步写
    void getReadableChunkInfo(char** buf, int* len) {
        if (chunks_.empty()) {
            *buf = nullptr;
            *len = 0;
            return;
        }
        BufferChunk* first = chunks_.front();
        *buf = first->data + first->readPos;
        *len = first->readableBytes();
    }


    ssize_t writeFd(int fd, int* savedErrno) {
        if (chunks_.empty()) return 0;
        
        std::vector<struct iovec> vec(chunks_.size());
        for (size_t i = 0; i < chunks_.size(); ++i) {
            vec[i].iov_base = chunks_[i]->data + chunks_[i]->readPos;
            vec[i].iov_len = chunks_[i]->readableBytes();
        }
        
        ssize_t n = writev(fd, vec.data(), vec.size());
        if (n < 0) {
            *savedErrno = errno;
        } else {
            retrieve(n);
        }
        return n;
    }
    
    // 支持按字符串位置查找 (用于状态机解析 \r\n 等)
    size_t find(const std::string& target) const {
        std::string content = peekAllAsString();
        return content.find(target);
    }
    
private:
    std::vector<BufferChunk*> chunks_;
    int totalBytes_;
};

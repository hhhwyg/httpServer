#pragma once
#include <mutex>
#include <vector>

const int CHUNK_SIZE = 4096;

class MemoryPool {
public:
    static MemoryPool& Instance() {
        static MemoryPool pool;
        return pool;
    }

    char* allocateChunk() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!freeChunks_.empty()) {
            char* chunk = freeChunks_.back();
            freeChunks_.pop_back();
            return chunk;
        }
        return new char[CHUNK_SIZE];
    }

    void deallocateChunk(char* chunk) {
        if (chunk) {
            std::lock_guard<std::mutex> lock(mutex_);
            freeChunks_.push_back(chunk);
        }
    }

private:
    MemoryPool() {}
    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (char* chunk : freeChunks_) {
            delete[] chunk;
        }
        freeChunks_.clear();
    }

    std::mutex mutex_;
    std::vector<char*> freeChunks_;
};

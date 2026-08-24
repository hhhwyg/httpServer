#pragma once
#include <cstddef>
#include <mutex>
#include <memory>
#include <utility>
#include <vector>

template <class T>
class ObjectPool {
public:
    static ObjectPool& Instance() {
        static ObjectPool pool;
        return pool;
    }

    void setMaxCached(std::size_t maxCached) {
        std::lock_guard<std::mutex> lock(mutex_);
        maxCached_ = maxCached;
        while (freeList_.size() > maxCached_) {
            T* storage = freeList_.back();
            freeList_.pop_back();
            std::allocator_traits<Allocator>::deallocate(allocator_, storage, 1);
        }
    }

    std::size_t maxCached() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return maxCached_;
    }

    std::size_t cachedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return freeList_.size();
    }

    template <typename... Args>
    std::shared_ptr<T> acquire(Args&&... args) {
        T* storage = nullptr;
        bool reused = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!freeList_.empty()) {
                storage = freeList_.back();
                freeList_.pop_back();
                reused = true;
            }
        }

        if (storage == nullptr) {
            storage = std::allocator_traits<Allocator>::allocate(allocator_, 1);
        }

        try {
            std::allocator_traits<Allocator>::construct(
                allocator_, storage, std::forward<Args>(args)...);
        } catch (...) {
            if (reused) {
                std::lock_guard<std::mutex> lock(mutex_);
                freeList_.push_back(storage);
            } else {
                std::allocator_traits<Allocator>::deallocate(allocator_, storage, 1);
            }
            throw;
        }

        return std::shared_ptr<T>(storage, [this](T* ptr) {
            this->release(ptr);
        });
    }

private:
    ObjectPool() : maxCached_(kDefaultMaxCached) {}
    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (T* storage : freeList_) {
            std::allocator_traits<Allocator>::deallocate(allocator_, storage, 1);
        }
        freeList_.clear();
    }

    void release(T* obj) {
        if (obj) {
            // Destroy the object before caching its raw storage. Reusing a live
            // HttpData object leaves the old fd, Channel and connection state behind.
            std::allocator_traits<Allocator>::destroy(allocator_, obj);
            bool shouldCache = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (freeList_.size() < maxCached_) {
                    freeList_.push_back(obj);
                    shouldCache = true;
                }
            }
            if (!shouldCache) {
                std::allocator_traits<Allocator>::deallocate(allocator_, obj, 1);
            }
        }
    }

    using Allocator = std::allocator<T>;
    static constexpr std::size_t kDefaultMaxCached = 1024;

    mutable std::mutex mutex_;
    Allocator allocator_;
    std::vector<T*> freeList_;
    std::size_t maxCached_;
};

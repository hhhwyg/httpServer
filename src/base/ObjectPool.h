// @Author Lin Ya
// @Email xxbbb@vip.qq.com
#pragma once
#include <mutex>
#include <vector>
#include <memory>

template <class T>
class ObjectPool {
public:
    static ObjectPool& Instance() {
        static ObjectPool pool;
        return pool;
    }

    template <typename... Args>
    std::shared_ptr<T> acquire(Args&&... args) {
        T* obj = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!freeList_.empty()) {
                obj = freeList_.back();
                freeList_.pop_back();
            }
        }

        if (obj == nullptr) {
            obj = new T(std::forward<Args>(args)...);
        } else {
            // Re-initialize existing object if possible (placement new or init function)
            // It relies on T having an init() or reset() to clear old state.
            // new (obj) T(std::forward<Args>(args)...); // placement new can be dangerous for object graphs, 
            // usually it's cleaner if T has an init/reset method which HttpData does.
        }

        // Return a shared_ptr with a custom deleter that returns the object to the pool instead of deleting it.
        return std::shared_ptr<T>(obj, [this](T* ptr) {
            this->release(ptr);
        });
    }

private:
    ObjectPool() {}
    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (T* ptr : freeList_) {
            delete ptr;
        }
        freeList_.clear();
    }

    void release(T* obj) {
        if (obj) {
            std::lock_guard<std::mutex> lock(mutex_);
            freeList_.push_back(obj);
        }
    }

    std::mutex mutex_;
    std::vector<T*> freeList_;
};

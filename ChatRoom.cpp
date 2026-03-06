#include"ChatRoom.h"
#include"HttpData.h"



void ChatRoom::join(std::shared_ptr<HttpData> user) {
        std::lock_guard<std::mutex> lock(mtx_);
        users_[user->getFd()] = user;
    }
    
void ChatRoom::leave(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        users_.erase(fd);
    }


void ChatRoom::broadcast(const std::string& msg, int sendFd) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto it = users_.begin(); it != users_.end(); ) {
            // 尝试提升为 shared_ptr
            if (auto shared_user = it->second.lock()) {
                if (shared_user->getFd() != sendFd) {
                    shared_user->sendMsg(msg); // 你需要在 HttpData 中实现异步发送函数
                }
                ++it;
            } else {
                it = users_.erase(it); // 用户已析构，自动清理
            }
        }
    }
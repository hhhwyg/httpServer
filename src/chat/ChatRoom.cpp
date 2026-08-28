#include"ChatRoom.h"



void ChatRoom::join(std::shared_ptr<httpserver::ConnectionSession> user) {
        std::lock_guard<std::mutex> lock(mtx_);
        users_[user->fd()] = user;
    }
    
void ChatRoom::leave(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        users_.erase(fd);
    }


void ChatRoom::broadcast(const std::string& msg, int sendFd) {
        std::vector<std::shared_ptr<httpserver::ConnectionSession>> targets;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (auto it = users_.begin(); it != users_.end(); ) {
                // 尝试提升为 shared_ptr
                if (auto shared_user = it->second.lock()) {
                    if (shared_user->fd() != sendFd) {
                        targets.push_back(shared_user);
                    }
                    ++it;
                } else {
                    it = users_.erase(it); // 用户已析构，自动清理
                }
            }
        }
        
        // 在锁的高优临界区之外进行真实的发送任务，防止 HttpData 内部发送阻塞或产生循环死锁锁争用
        for (auto& user : targets) {
            user->sendMessage(msg);
        }
    }

bool ChatRoom::contains(int fd) const {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = users_.find(fd);
        return it != users_.end() && !it->second.expired();
    }

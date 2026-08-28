#pragma once
#include<mutex>
#include<memory>
#include<unordered_map>
#include<random>
#include<string>
#include<vector>

#include"httpserver/application/ApplicationContext.h"

class ChatRoom {
public:
    std::string roomId_;
    std::string name_;
    
    void join(std::shared_ptr<httpserver::ConnectionSession> user);
    void leave(int fd);
    bool contains(int fd) const;
    void broadcast(const std::string& msg, int skipFd);
    ChatRoom(std::string roomId,std::string name): roomId_(roomId),name_(name) {}
    
private:
    mutable std::mutex mtx_;
    std::unordered_map<int, std::weak_ptr<httpserver::ConnectionSession>> users_;

};

#pragma once
#include<mutex>
#include<memory>
#include<unordered_map>
#include<random>

class HttpData;

class ChatRoom {
public:
    std::string roomId_;
    std::string name_;
    
    void join(std::shared_ptr<HttpData>);
    void leave(int fd);
    void broadcast(const std::string& msg, int skipFd);
    ChatRoom(std::string roomId,std::string name): roomId_(roomId),name_(name) {}
    
private:
    std::mutex mtx_;
    std::unordered_map<int, std::weak_ptr<HttpData>> users_;

};
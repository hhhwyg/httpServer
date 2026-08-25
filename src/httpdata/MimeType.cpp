#include "httpserver/protocol/MimeType.h"

#include <pthread.h>
#include <unordered_map>

namespace {

pthread_once_t onceControl = PTHREAD_ONCE_INIT;
std::unordered_map<std::string, std::string> mime;

void initializeMimeTypes() {
  mime[".html"] = "text/html";
  mime[".avi"] = "video/x-msvideo";
  mime[".bmp"] = "image/bmp";
  mime[".c"] = "text/plain";
  mime[".css"] = "text/css";
  mime[".js"] = "application/javascript";
  mime[".json"] = "application/json";
  mime[".doc"] = "application/msword";
  mime[".gif"] = "image/gif";
  mime[".gz"] = "application/x-gzip";
  mime[".htm"] = "text/html";
  mime[".icg"] = "image/png";
  mime[".txo"] = "image/x-icon";
  mime[".jpg"] = "image/jpeg";
  mime[".pnt"] = "text/plain";
  mime[".mp3"] = "audio/mp3";
  mime["default"] = "text/html";
}

}  // namespace

namespace httpserver {

std::string MimeType::getMime(std::string_view suffix) {
  pthread_once(&onceControl, initializeMimeTypes);
  const auto it = mime.find(std::string(suffix));
  return it == mime.end() ? mime["default"] : it->second;
}

}  // namespace httpserver

#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace httpserver {

inline constexpr int kSmallBuffer = 4000;
inline constexpr int kLargeBuffer = 4000 * 1000;

template <int Size>
class FixedBuffer {
 public:
  FixedBuffer() : current_(data_) {}
  FixedBuffer(const FixedBuffer&) = delete;
  FixedBuffer& operator=(const FixedBuffer&) = delete;

  void append(const char* data, std::size_t length) {
    if (avail() > static_cast<int>(length)) {
      std::memcpy(current_, data, length);
      current_ += length;
    }
  }

  const char* data() const { return data_; }
  int length() const { return static_cast<int>(current_ - data_); }
  char* current() { return current_; }
  int avail() const { return static_cast<int>(end() - current_); }
  void add(std::size_t length) { current_ += length; }
  void reset() { current_ = data_; }
  void bzero() { std::memset(data_, 0, sizeof(data_)); }

 private:
  const char* end() const { return data_ + sizeof(data_); }

  char data_[Size];
  char* current_;
};

class LogStream {
 public:
  using Buffer = FixedBuffer<kSmallBuffer>;

  LogStream() = default;
  LogStream(const LogStream&) = delete;
  LogStream& operator=(const LogStream&) = delete;

  LogStream& operator<<(bool value) {
    buffer_.append(value ? "1" : "0", 1);
    return *this;
  }

  LogStream& operator<<(short value);
  LogStream& operator<<(unsigned short value);
  LogStream& operator<<(int value);
  LogStream& operator<<(unsigned int value);
  LogStream& operator<<(long value);
  LogStream& operator<<(unsigned long value);
  LogStream& operator<<(long long value);
  LogStream& operator<<(unsigned long long value);
  LogStream& operator<<(const void* value);

  LogStream& operator<<(float value) {
    return *this << static_cast<double>(value);
  }
  LogStream& operator<<(double value);
  LogStream& operator<<(long double value);

  LogStream& operator<<(char value) {
    buffer_.append(&value, 1);
    return *this;
  }

  LogStream& operator<<(const char* value) {
    if (value != nullptr) {
      buffer_.append(value, std::strlen(value));
    } else {
      buffer_.append("(null)", 6);
    }
    return *this;
  }

  LogStream& operator<<(const unsigned char* value) {
    return *this << reinterpret_cast<const char*>(value);
  }

  LogStream& operator<<(const std::string& value) {
    buffer_.append(value.c_str(), value.size());
    return *this;
  }

  void append(const char* data, int length) { buffer_.append(data, length); }
  const Buffer& buffer() const { return buffer_; }
  void resetBuffer() { buffer_.reset(); }

 private:
  template <typename T>
  void formatInteger(T value);

  Buffer buffer_;
  static constexpr int kMaxNumericSize = 32;
};

}  // namespace httpserver

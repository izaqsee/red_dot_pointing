#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class Stream {
public:
  std::string input, output;
  size_t position = 0;
  int available() { return int(input.size() - position); }
  int read() { return available() ? static_cast<unsigned char>(input[position++]) : -1; }
  void print(const char *text) { output += text; }
  void print(float value, int digits) {
    char text[64];
    snprintf(text, sizeof(text), "%.*f", digits, double(value));
    output += text;
  }
  void println(const char *text) { output += text; output += "\r\n"; }
};

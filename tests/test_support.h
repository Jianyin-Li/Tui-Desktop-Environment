#pragma once

#include <iostream>
#include <string>

namespace tdesktop::test {

inline int failures = 0;

inline void Fail(const char* file, int line, const std::string& message) {
  std::cerr << file << ':' << line << ": " << message << '\n';
  ++failures;
}

template <typename A, typename B>
void CheckEqual(const char* file,
                int line,
                const char* left_text,
                const char* right_text,
                const A& left,
                const B& right) {
  if (!(left == right)) {
    std::cerr << file << ':' << line << ": expected " << left_text
              << " == " << right_text << '\n';
    ++failures;
  }
}

inline int Finish() {
  if (failures == 0) {
    std::cout << "PASS\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}

}  // namespace tdesktop::test

#define CHECK_TRUE(condition)                                              \
  do {                                                                     \
    if (!(condition)) {                                                    \
      ::tdesktop::test::Fail(__FILE__, __LINE__, "CHECK_TRUE(" #condition ")"); \
    }                                                                      \
  } while (false)

#define CHECK_FALSE(condition) CHECK_TRUE(!(condition))

#define CHECK_EQ(left, right)                                              \
  ::tdesktop::test::CheckEqual(__FILE__, __LINE__, #left, #right, left, right)

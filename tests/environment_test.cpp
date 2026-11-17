#include "platform/environment.h"
#include "test_support.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

void TestFromEnvironPreservesOrderAndDuplicates() {
  char first[] = "A=1";
  char empty[] = "B=";
  char duplicate[] = "A=2";
  char* envp[] = {first, empty, duplicate, nullptr};

  const tdesktop::Environment environment =
      tdesktop::Environment::FromEnviron(envp);

  CHECK_EQ(environment.size(), std::size_t{3});
  CHECK_TRUE(environment.Contains("A"));
  CHECK_TRUE(environment.Contains("B"));
  CHECK_FALSE(environment.Contains("C"));
  CHECK_EQ(environment.Get("A").value_or(""), std::string("1"));
  CHECK_EQ(environment.Get("B").value_or("missing"), std::string(""));

  const std::vector<std::string> strings = environment.ToStrings();
  CHECK_EQ(strings.size(), std::size_t{3});
  CHECK_EQ(strings[0], std::string("A=1"));
  CHECK_EQ(strings[1], std::string("B="));
  CHECK_EQ(strings[2], std::string("A=2"));
}

void TestSetReplacesAllDuplicates() {
  char first[] = "A=1";
  char duplicate[] = "A=2";
  char other[] = "B=3";
  char* envp[] = {first, duplicate, other, nullptr};

  tdesktop::Environment environment =
      tdesktop::Environment::FromEnviron(envp);
  environment.Set("A", "4");

  CHECK_EQ(environment.size(), std::size_t{2});
  CHECK_EQ(environment.Get("A").value_or(""), std::string("4"));
  CHECK_EQ(environment.GetOr("B", ""), std::string("3"));
}

void TestUnsetRemovesAllMatches() {
  char first[] = "A=1";
  char duplicate[] = "A=2";
  char other[] = "B=3";
  char* envp[] = {first, duplicate, other, nullptr};

  tdesktop::Environment environment =
      tdesktop::Environment::FromEnviron(envp);
  environment.Unset("A");

  CHECK_EQ(environment.size(), std::size_t{1});
  CHECK_FALSE(environment.Contains("A"));
  CHECK_EQ(environment.GetOr("B", ""), std::string("3"));

  environment.Unset("missing");
  CHECK_EQ(environment.size(), std::size_t{1});
}

void TestCurrentReflectsProcessEnvironment() {
  CHECK_EQ(setenv("TDESKTOP_ENV_TEST", "present", 1), 0);
  CHECK_EQ(tdesktop::Environment::Current().GetOr("TDESKTOP_ENV_TEST", ""),
           std::string("present"));
  CHECK_EQ(unsetenv("TDESKTOP_ENV_TEST"), 0);
  CHECK_FALSE(tdesktop::Environment::Current().Contains("TDESKTOP_ENV_TEST"));
}

}  // namespace

int main() {
  TestFromEnvironPreservesOrderAndDuplicates();
  TestSetReplacesAllDuplicates();
  TestUnsetRemovesAllMatches();
  TestCurrentReflectsProcessEnvironment();
  return tdesktop::test::Finish();
}

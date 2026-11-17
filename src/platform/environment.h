#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tdesktop {

struct EnvironmentEntry {
  std::string name;
  std::string value;
};

// A snapshot of process environment variables.
//
// The direct launch path must preserve the caller's environment exactly:
// FromEnviron() keeps entry order and duplicate names. Mutating helpers are
// only for building per-pane overrides from a snapshot.
class Environment {
 public:
  Environment() = default;

  static Environment FromEnviron(char* const* envp);
  static Environment Current();

  bool Contains(std::string_view name) const;
  std::optional<std::string> Get(std::string_view name) const;
  std::string GetOr(std::string_view name, std::string_view fallback) const;

  // Replaces every existing entry with |name| and appends the new value.
  void Set(std::string name, std::string value);
  void Unset(std::string_view name);

  const std::vector<EnvironmentEntry>& entries() const { return entries_; }
  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }

  // Returns "NAME=value" in the original order.
  std::vector<std::string> ToStrings() const;

 private:
  std::vector<EnvironmentEntry> entries_;
};

}  // namespace tdesktop

#include "platform/environment.h"

#include <algorithm>
#include <utility>

extern char** environ;

namespace tdesktop {
namespace {

bool NameEquals(const EnvironmentEntry& entry, std::string_view name) {
  return entry.name == name;
}

}  // namespace

Environment Environment::FromEnviron(char* const* envp) {
  Environment environment;
  if (envp == nullptr) {
    return environment;
  }

  for (char* const* item = envp; *item != nullptr; ++item) {
    const std::string_view raw(*item);
    const std::size_t separator = raw.find('=');
    if (separator == std::string_view::npos) {
      environment.entries_.push_back({std::string(raw), std::string()});
      continue;
    }
    environment.entries_.push_back({
        std::string(raw.substr(0, separator)),
        std::string(raw.substr(separator + 1)),
    });
  }
  return environment;
}

Environment Environment::Current() {
  return FromEnviron(environ);
}

bool Environment::Contains(std::string_view name) const {
  return std::any_of(entries_.begin(), entries_.end(),
                     [name](const EnvironmentEntry& entry) {
                       return NameEquals(entry, name);
                     });
}

std::optional<std::string> Environment::Get(std::string_view name) const {
  for (const EnvironmentEntry& entry : entries_) {
    if (NameEquals(entry, name)) {
      return entry.value;
    }
  }
  return std::nullopt;
}

std::string Environment::GetOr(std::string_view name,
                               std::string_view fallback) const {
  const std::optional<std::string> value = Get(name);
  return value.has_value() ? *value : std::string(fallback);
}

void Environment::Set(std::string name, std::string value) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [&name](const EnvironmentEntry& entry) {
                       return entry.name == name;
                     }),
      entries_.end());
  entries_.push_back({std::move(name), std::move(value)});
}

void Environment::Unset(std::string_view name) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [name](const EnvironmentEntry& entry) {
                       return NameEquals(entry, name);
                     }),
      entries_.end());
}

std::vector<std::string> Environment::ToStrings() const {
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const EnvironmentEntry& entry : entries_) {
    result.push_back(entry.name + "=" + entry.value);
  }
  return result;
}

}  // namespace tdesktop

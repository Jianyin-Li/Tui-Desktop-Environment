#pragma once

#include "platform/environment.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <vector>

namespace tdesktop {

enum class LaunchMode {
  kDirect,   // Started from a shell: inherit identity, cwd and environment.
  kManaged,  // Started by tdesktop-manager from a systemd target.
};

struct LaunchContext {
  LaunchMode mode = LaunchMode::kDirect;
  uid_t uid = 0;
  gid_t gid = 0;
  std::vector<gid_t> groups;
  std::filesystem::path cwd;
  Environment env;
  std::string tty;         // Empty when stdin is not a terminal.
  std::string session_id;  // Managed mode only.

  bool managed() const { return mode == LaunchMode::kManaged; }
};

// Captures getuid(), getgid(), getgroups(), getcwd() and environ without
// modifying any of them. This is the only constructor used by `tdesktop`.
LaunchContext BuildDirectLaunchContext();

// Used by tdesktop-manager (target launch only). The caller owns PAM/logind
// setup and environment composition; this function performs no privilege or
// identity changes.
LaunchContext BuildManagedLaunchContext(Environment env,
                                        uid_t uid,
                                        gid_t gid,
                                        std::vector<gid_t> groups,
                                        std::filesystem::path cwd,
                                        std::string tty,
                                        std::string session_id);

struct LaunchDiagnostics {
  LaunchMode mode = LaunchMode::kDirect;
  uid_t uid = 0;
  gid_t gid = 0;
  std::size_t group_count = 0;
  std::string cwd;
  std::string tty;
  std::string term;
  std::string user;
  std::size_t environment_count = 0;
  bool manager_used = false;
  bool pam_session = false;

  // Human-readable, deliberately excludes sensitive environment values.
  std::string ToString() const;
};

LaunchDiagnostics Diagnose(const LaunchContext& context);

}  // namespace tdesktop

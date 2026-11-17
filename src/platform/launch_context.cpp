#include "platform/launch_context.h"

#include <unistd.h>

#include <cerrno>
#include <sstream>
#include <system_error>
#include <utility>

namespace tdesktop {
namespace {

std::vector<gid_t> CurrentGroups() {
  const int count = getgroups(0, nullptr);
  if (count <= 0) {
    return {};
  }

  std::vector<gid_t> groups(static_cast<std::size_t>(count));
  const int actual = getgroups(count, groups.data());
  if (actual < 0) {
    return {};
  }
  groups.resize(static_cast<std::size_t>(actual));
  return groups;
}

std::string CurrentTty() {
  if (isatty(STDIN_FILENO) == 0) {
    return {};
  }
  const char* name = ttyname(STDIN_FILENO);
  return name == nullptr ? std::string() : std::string(name);
}

}  // namespace

LaunchContext BuildDirectLaunchContext() {
  LaunchContext context;
  context.mode = LaunchMode::kDirect;
  context.uid = getuid();
  context.gid = getgid();
  context.groups = CurrentGroups();
  context.env = Environment::Current();
  context.tty = CurrentTty();

  std::error_code error;
  context.cwd = std::filesystem::current_path(error);
  if (error) {
    context.cwd.clear();
  }

  return context;
}

LaunchContext BuildManagedLaunchContext(Environment env,
                                        uid_t uid,
                                        gid_t gid,
                                        std::vector<gid_t> groups,
                                        std::filesystem::path cwd,
                                        std::string tty,
                                        std::string session_id) {
  LaunchContext context;
  context.mode = LaunchMode::kManaged;
  context.env = std::move(env);
  context.uid = uid;
  context.gid = gid;
  context.groups = std::move(groups);
  context.cwd = std::move(cwd);
  context.tty = std::move(tty);
  context.session_id = std::move(session_id);
  return context;
}

std::string LaunchDiagnostics::ToString() const {
  std::ostringstream out;
  out << "launch mode: " << (mode == LaunchMode::kManaged ? "managed" : "direct")
      << '\n';
  out << "manager used: " << (manager_used ? "yes" : "no") << '\n';
  out << "PAM session: " << (pam_session ? "yes" : "no") << '\n';
  out << "uid: " << uid << '\n';
  out << "gid: " << gid << '\n';
  out << "groups: " << group_count << '\n';
  out << "cwd: " << cwd << '\n';
  out << "tty: " << (tty.empty() ? "(none)" : tty) << '\n';
  out << "TERM: " << (term.empty() ? "(unset)" : term) << '\n';
  out << "USER: " << (user.empty() ? "(unset)" : user) << '\n';
  out << "environment entries: " << environment_count << '\n';
  return out.str();
}

LaunchDiagnostics Diagnose(const LaunchContext& context) {
  LaunchDiagnostics diagnostics;
  diagnostics.mode = context.mode;
  diagnostics.uid = context.uid;
  diagnostics.gid = context.gid;
  diagnostics.group_count = context.groups.size();
  diagnostics.cwd = context.cwd.string();
  diagnostics.tty = context.tty;
  diagnostics.term = context.env.GetOr("TERM", "");
  diagnostics.user = context.env.GetOr("USER", "");
  diagnostics.environment_count = context.env.size();
  diagnostics.manager_used = context.managed();
  diagnostics.pam_session = context.managed();
  return diagnostics;
}

}  // namespace tdesktop

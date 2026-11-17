#include "platform/launch_context.h"
#include "test_support.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void TestDirectContextMatchesProcessIdentity() {
  const tdesktop::LaunchContext context =
      tdesktop::BuildDirectLaunchContext();

  CHECK_TRUE(context.mode == tdesktop::LaunchMode::kDirect);
  CHECK_FALSE(context.managed());
  CHECK_TRUE(context.uid == getuid());
  CHECK_TRUE(context.gid == getgid());
  CHECK_TRUE(!context.cwd.empty());
  CHECK_TRUE(context.cwd == std::filesystem::current_path());
  CHECK_TRUE(context.session_id.empty());
  CHECK_TRUE(context.tty.empty() || context.tty.rfind("/dev/", 0) == 0);
}

void TestDirectContextSnapshotsEnvironment() {
  CHECK_EQ(setenv("TDESKTOP_CONTEXT_TEST", "before", 1), 0);
  const tdesktop::LaunchContext context =
      tdesktop::BuildDirectLaunchContext();
  CHECK_EQ(context.env.GetOr("TDESKTOP_CONTEXT_TEST", ""), std::string("before"));

  CHECK_EQ(setenv("TDESKTOP_CONTEXT_TEST", "after", 1), 0);
  CHECK_EQ(context.env.GetOr("TDESKTOP_CONTEXT_TEST", ""), std::string("before"));
  CHECK_EQ(tdesktop::Environment::Current().GetOr("TDESKTOP_CONTEXT_TEST", ""),
           std::string("after"));

  CHECK_EQ(unsetenv("TDESKTOP_CONTEXT_TEST"), 0);
}

void TestDirectDiagnosticsDoNotClaimManager() {
  const tdesktop::LaunchContext context =
      tdesktop::BuildDirectLaunchContext();
  const tdesktop::LaunchDiagnostics diagnostics =
      tdesktop::Diagnose(context);

  CHECK_TRUE(diagnostics.mode == tdesktop::LaunchMode::kDirect);
  CHECK_FALSE(diagnostics.manager_used);
  CHECK_FALSE(diagnostics.pam_session);
  CHECK_TRUE(diagnostics.uid == getuid());
  CHECK_TRUE(diagnostics.gid == getgid());
  CHECK_TRUE(!diagnostics.cwd.empty());
  CHECK_EQ(diagnostics.environment_count, context.env.size());

  const std::string text = diagnostics.ToString();
  CHECK_TRUE(text.find("launch mode: direct") != std::string::npos);
  CHECK_TRUE(text.find("manager used: no") != std::string::npos);
  CHECK_TRUE(text.find("PAM session: no") != std::string::npos);
}

void TestManagedContextUsesSuppliedEnvironment() {
  tdesktop::Environment env;
  env.Set("TERM", "linux");
  env.Set("USER", "managed-user");
  env.Set("TDESKTOP_MANAGED", "1");

  const tdesktop::LaunchContext context =
      tdesktop::BuildManagedLaunchContext(
          env, 1000, 1000, std::vector<gid_t>{1000},
          std::filesystem::path("/home/managed-user"), "/dev/tty12",
          "session-1");

  CHECK_TRUE(context.managed());
  CHECK_TRUE(context.mode == tdesktop::LaunchMode::kManaged);
  CHECK_EQ(context.env.GetOr("TERM", ""), std::string("linux"));
  CHECK_EQ(context.cwd.string(), std::string("/home/managed-user"));
  CHECK_EQ(context.session_id, std::string("session-1"));

  const tdesktop::LaunchDiagnostics diagnostics =
      tdesktop::Diagnose(context);
  CHECK_TRUE(diagnostics.manager_used);
  CHECK_TRUE(diagnostics.pam_session);
  CHECK_TRUE(diagnostics.ToString().find("launch mode: managed") !=
             std::string::npos);
}

}  // namespace

int main() {
  TestDirectContextMatchesProcessIdentity();
  TestDirectContextSnapshotsEnvironment();
  TestDirectDiagnosticsDoNotClaimManager();
  TestManagedContextUsesSuppliedEnvironment();
  return tdesktop::test::Finish();
}

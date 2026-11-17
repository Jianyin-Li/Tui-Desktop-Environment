#include "platform/environment.h"
#include "platform/launch_context.h"
#include "terminal/pty_session.h"
#include "ui/terminal_app.h"

#include <tdesktop/version.h>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/terminal.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void PrintUsage(std::ostream& out) {
  out << "tdesktop " TDESKTOP_VERSION "\n"
      << "\n"
      << "Usage: tdesktop [options]\n"
      << "\n"
      << "Options:\n"
      << "  --diagnostics   Print launch identity/environment information\n"
      << "  --version, -v   Print version\n"
      << "  --help, -h      Print this help\n"
      << "\n"
      << "Without options, starts the TTY UI and an interactive shell.\n";
}

int PrintDiagnostics() {
  const tdesktop::LaunchContext context =
      tdesktop::BuildDirectLaunchContext();
  std::cout << "tdesktop " TDESKTOP_VERSION "\n"
            << tdesktop::Diagnose(context).ToString();
  return 0;
}

void ResetTerminal() {
  constexpr char kReset[] = "\x1b[0m\x1b[?25h\x1b[2J\x1b[H";
  (void)write(STDOUT_FILENO, kReset, sizeof(kReset) - 1);
}

int RunUi() {
  if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0) {
    std::cerr << "tdesktop: the UI requires a TTY\n"
              << "Use `tdesktop --diagnostics` for environment information.\n";
    return 1;
  }

  const std::string term =
      tdesktop::Environment::Current().GetOr("TERM", std::string());
  if (term == "linux") {
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Palette16);
  }

  tdesktop::PtySession session;
  int result = 0;

  {
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen.TrackMouse(false);

    const std::string shell =
        tdesktop::Environment::Current().GetOr("SHELL", "/bin/sh");
    const int rows = std::max(1, screen.dimy() - 2);
    const int cols = std::max(1, screen.dimx());

    std::string error;
    if (!session.Start(shell, rows, cols, &error)) {
      std::cerr << "tdesktop: failed to start shell: " << error << '\n';
      result = 1;
    } else {
      session.SetScreen(&screen);
      tdesktop::TerminalApp app(session, screen);
      screen.Loop(app.Root());
      session.SetScreen(nullptr);
    }
  }

  session.Stop();
  ResetTerminal();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--version" || argument == "-v") {
      std::cout << "tdesktop " TDESKTOP_VERSION << '\n';
      return 0;
    }
    if (argument == "--help" || argument == "-h") {
      PrintUsage(std::cout);
      return 0;
    }
    if (argument == "--diagnostics") {
      return PrintDiagnostics();
    }

    std::cerr << "tdesktop: unknown option: " << argument << "\n";
    PrintUsage(std::cerr);
    return 2;
  }

  return RunUi();
}

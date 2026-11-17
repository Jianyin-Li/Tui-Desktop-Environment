#include "platform/launch_context.h"

#include <tdesktop/version.h>

#include <iostream>
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
      << "  --help, -h      Print this help\n";
}

int PrintDiagnostics() {
  const tdesktop::LaunchContext context =
      tdesktop::BuildDirectLaunchContext();
  std::cout << "tdesktop " TDESKTOP_VERSION "\n"
            << tdesktop::Diagnose(context).ToString();
  return 0;
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

  std::cout << "tdesktop " TDESKTOP_VERSION " (environment layer)\n"
            << "The TTY UI is not implemented yet.\n"
            << "Run `tdesktop --diagnostics` to inspect the launch context.\n";
  return 0;
}

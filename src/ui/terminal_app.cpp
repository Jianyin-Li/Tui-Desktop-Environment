#include "ui/terminal_app.h"

#include "platform/environment.h"
#include "ui/terminal_view.h"

#include <tdesktop/version.h>

#include <ftxui/component/event.hpp>

#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <string>

namespace tdesktop {
namespace {

std::string InputOf(const ftxui::Event& event) {
  std::string input = event.input();
  while (!input.empty() && input.back() == '\0') {
    input.pop_back();
  }
  return input;
}

bool IsQuitChord(const std::string& input) {
  return input == "\x1bX" || input == std::string(1, static_cast<char>(0xD8));
}

bool IsHideChord(const std::string& input) {
  return input == "\x1bH" || input == std::string(1, static_cast<char>(0xC8));
}

bool IsCloseAppChord(const std::string& input) {
  return input == "\x1bQ" || input == std::string(1, static_cast<char>(0xD1));
}

}  // namespace

TerminalApp::TerminalApp(PtySession& session,
                         ftxui::ScreenInteractive& screen)
    : session_(session),
      screen_(screen),
      shell_(Environment::Current().GetOr("SHELL", "/bin/sh")) {}

ftxui::Component TerminalApp::Root() {
  auto terminal = std::make_shared<TerminalView>(session_);
  terminal_element_ =
      std::static_pointer_cast<ftxui::Node>(terminal) | ftxui::flex;

  auto renderer = ftxui::Renderer([this] {
    return hidden_ ? RenderHome() : RenderMain();
  });

  return ftxui::CatchEvent(renderer,
                           [this](ftxui::Event event) {
                             return OnEvent(std::move(event));
                           });
}

bool TerminalApp::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Custom) {
    if (!hidden_ && !session_.alive()) {
      hidden_ = true;
      message_ = "应用已退出";
    }
    return false;
  }

  const std::string input = InputOf(event);

  // Exit only the TDESKTOP UI process. A future target-only manager owns
  // session lifetime; this key must not signal or kill it.
  if (IsQuitChord(input)) {
    screen_.Exit();
    return true;
  }

  if (IsHideChord(input)) {
    if (hidden_) {
      ShowApp();
    } else {
      hidden_ = true;
    }
    return true;
  }

  // Close the current TUI program and return to the desktop. This does not
  // quit TDESKTOP.
  if (IsCloseAppChord(input)) {
    ShowHome();
    return true;
  }

  if (hidden_) {
    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::TabReverse) {
      selected_tile_ = (selected_tile_ + 1) % 2;
      return true;
    }
    if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Tab) {
      selected_tile_ = (selected_tile_ + 1) % 2;
      return true;
    }
    if (event == ftxui::Event::Return || input == "\n" || input == "\r") {
      if (selected_tile_ == 0) {
        ShowApp();
      } else {
        screen_.Exit();
      }
      return true;
    }
    return true;
  }

  if (event.is_mouse()) {
    return true;
  }
  if (input.empty()) {
    return false;
  }

  session_.WriteInput(input);
  return true;
}

bool TerminalApp::StartSession() {
  if (session_.alive()) {
    return true;
  }

  // Reap/clean a previously exited session before starting a new one.
  session_.Stop();
  const int rows = std::max(1, screen_.dimy() - 2);
  const int cols = std::max(1, screen_.dimx());
  std::string error;
  if (!session_.Start(shell_, rows, cols, &error)) {
    message_ = "无法启动 shell: " + error;
    return false;
  }
  message_.clear();
  return true;
}

void TerminalApp::ShowApp() {
  if (!session_.alive() && !StartSession()) {
    hidden_ = true;
    return;
  }
  hidden_ = false;
}

void TerminalApp::ShowHome() {
  if (session_.alive()) {
    session_.Stop();
    message_ = "应用已关闭";
  } else if (message_.empty()) {
    message_ = "没有运行中的应用";
  }
  hidden_ = true;
}

std::string TerminalApp::Clock() const {
  const std::time_t now = std::time(nullptr);
  std::tm local {};
  if (localtime_r(&now, &local) == nullptr) {
    return "--:--";
  }
  char buffer[16] = {};
  if (std::strftime(buffer, sizeof(buffer), "%H:%M", &local) == 0) {
    return "--:--";
  }
  return buffer;
}

std::string TerminalApp::HostName() const {
  char hostname[128] = {};
  if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
    return "tty";
  }
  return hostname;
}

ftxui::Element TerminalApp::Header() {
  if (hidden_) {
    return ftxui::hbox({
        ftxui::text(" TDESKTOP ") | ftxui::bold |
            ftxui::color(ftxui::Color::Cyan),
        ftxui::filler(),
        ftxui::text(" " + HostName() + "  " + Clock() + " ") |
            ftxui::color(ftxui::Color::White),
    });
  }

  std::string title = session_.Title();
  if (title.empty()) {
    title = "shell";
  }
  const std::string status = session_.alive() ? "running" : "exited";
  return ftxui::hbox({
      ftxui::text(" TDESKTOP ") | ftxui::bold |
          ftxui::color(ftxui::Color::Cyan),
      ftxui::text(" " + title + " ") | ftxui::flex |
          ftxui::color(ftxui::Color::White),
      ftxui::text(" " + status + " ") | ftxui::dim,
  });
}

ftxui::Element TerminalApp::Footer() {
  if (hidden_) {
    return ftxui::hbox({
        ftxui::text("  [Terminal]  ") |
            ftxui::color(session_.alive() ? ftxui::Color::GreenLight
                                          : ftxui::Color::GrayLight),
        ftxui::text("  [Exit]  ") | ftxui::dim,
        ftxui::filler(),
        ftxui::text("←/→ 选择  Enter 打开  Alt+Shift+X 退出 TUI ") |
            ftxui::dim,
    });
  }

  return ftxui::hbox({
      ftxui::text(" Alt+Shift+H 隐藏   Alt+Shift+Q 关闭   Alt+Shift+X 退出 TUI ") |
          ftxui::dim,
      ftxui::filler(),
      ftxui::text("TDESKTOP " TDESKTOP_VERSION) | ftxui::dim,
  });
}

ftxui::Element TerminalApp::HomeTile(const std::string& icon,
                                     const std::string& name,
                                     const std::string& subtitle,
                                     bool selected) {
  auto content = ftxui::vbox({
      ftxui::text(icon) | ftxui::bold | ftxui::center,
      ftxui::text(name) | ftxui::bold | ftxui::center,
      ftxui::text(subtitle) | ftxui::center,
  });

  if (selected) {
    return content | ftxui::borderStyled(ftxui::Color::Cyan) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 24) |
           ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 7) |
           ftxui::color(ftxui::Color::Cyan) | ftxui::bold;
  }
  return content | ftxui::border |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 24) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 7);
}

ftxui::Element TerminalApp::RunningCard() {
  if (!session_.alive()) {
    if (!message_.empty()) {
      return ftxui::text(message_) | ftxui::color(ftxui::Color::RedLight) |
             ftxui::center;
    }
    return ftxui::text("没有运行中的应用") | ftxui::dim | ftxui::center;
  }

  std::string title = session_.Title();
  if (title.empty()) {
    title = "Shell";
  }
  return ftxui::vbox({
             ftxui::hbox({
                 ftxui::text(" ● ") | ftxui::color(ftxui::Color::GreenLight),
                 ftxui::text(title) | ftxui::bold | ftxui::flex,
                 ftxui::text("  PID " + std::to_string(session_.pid())) |
                     ftxui::dim,
             }),
             ftxui::text("Enter 打开    Alt+Shift+Q 关闭") | ftxui::dim,
         }) |
         ftxui::border;
}

ftxui::Element TerminalApp::RenderMain() {
  return ftxui::vbox({
      Header(),
      terminal_element_,
      Footer(),
  });
}

ftxui::Element TerminalApp::RenderHome() {
  auto tiles = ftxui::hbox({
      ftxui::filler(),
      HomeTile(">_", "Terminal",
               session_.alive() ? "运行中" : "打开 shell", selected_tile_ == 0),
      ftxui::text("   "),
      HomeTile("X", "Exit", "退出 TUI", selected_tile_ == 1),
      ftxui::filler(),
  });

  auto desktop = ftxui::vbox({
      Header(),
      ftxui::filler(),
      tiles,
      ftxui::text(""),
      RunningCard() | ftxui::hcenter,
      ftxui::filler(),
      Footer(),
  });

  return desktop;
}

}  // namespace tdesktop

#include "ui/terminal_app.h"

#include "ui/terminal_view.h"

#include <tdesktop/version.h>

#include <ftxui/component/event.hpp>

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

}  // namespace

TerminalApp::TerminalApp(PtySession& session,
                         ftxui::ScreenInteractive& screen)
    : session_(session), screen_(screen) {}

ftxui::Component TerminalApp::Root() {
  auto terminal = std::make_shared<TerminalView>(session_);
  terminal_node_ = terminal;
  terminal_element_ = std::static_pointer_cast<ftxui::Node>(terminal) | ftxui::flex;

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
    return false;
  }

  const std::string input = InputOf(event);
  if (IsQuitChord(input)) {
    screen_.Exit();
    return true;
  }
  if (IsHideChord(input)) {
    hidden_ = !hidden_;
    return true;
  }

  if (hidden_) {
    if (event == ftxui::Event::Return || input == "\n" || input == "\r") {
      hidden_ = false;
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

ftxui::Element TerminalApp::Header() {
  std::string title = session_.Title();
  if (title.empty()) {
    title = "shell";
  }
  const std::string status = session_.alive() ? "running" : "exited";
  return ftxui::hbox({
      ftxui::text(" TDESKTOP ") | ftxui::bold |
          ftxui::color(ftxui::Color::White) | ftxui::bgcolor(ftxui::Color::Blue),
      ftxui::text(" " + title + " ") | ftxui::flex,
      ftxui::text(" " + status + " ") | ftxui::dim,
  });
}

ftxui::Element TerminalApp::Footer() {
  const std::string toggle = hidden_ ? "恢复" : "隐藏";
  return ftxui::hbox({
      ftxui::text(" Alt+Shift+H ") | ftxui::bold,
      ftxui::text(toggle),
      ftxui::text("  Alt+Shift+X ") | ftxui::bold,
      ftxui::text("退出"),
      ftxui::text("  TDESKTOP " TDESKTOP_VERSION) | ftxui::dim |
          ftxui::flex,
  });
}

ftxui::Element TerminalApp::RenderMain() {
  return ftxui::vbox({
      Header(),
      terminal_element_,
      Footer(),
  });
}

ftxui::Element TerminalApp::RenderHome() {
  const std::string status = session_.alive() ? "运行中" : "已退出";
  auto content = ftxui::vbox({
                     ftxui::text("TDESKTOP " TDESKTOP_VERSION) | ftxui::bold,
                     ftxui::text(""),
                     ftxui::text("Shell  " + status) | ftxui::bold,
                     ftxui::text("PID " + std::to_string(session_.pid())) |
                         ftxui::dim,
                     ftxui::text(""),
                     ftxui::text("Enter  恢复"),
                     ftxui::text("Alt+Shift+X  退出桌面"),
                 }) |
                 ftxui::border | ftxui::center;

  return ftxui::vbox({
      Header(),
      content | ftxui::flex,
      Footer(),
  });
}

}  // namespace tdesktop

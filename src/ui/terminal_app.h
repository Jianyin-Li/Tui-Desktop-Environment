#pragma once

#include "terminal/pty_session.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <memory>
#include <utility>

namespace tdesktop {

class TerminalApp {
 public:
  TerminalApp(PtySession& session, ftxui::ScreenInteractive& screen);

  ftxui::Component Root();

 private:
  bool OnEvent(ftxui::Event event);
  ftxui::Element RenderMain();
  ftxui::Element RenderHome();
  ftxui::Element Header();
  ftxui::Element Footer();

  PtySession& session_;
  ftxui::ScreenInteractive& screen_;
  bool hidden_ = false;
  std::shared_ptr<ftxui::Node> terminal_node_;
  ftxui::Element terminal_element_;
};

}  // namespace tdesktop

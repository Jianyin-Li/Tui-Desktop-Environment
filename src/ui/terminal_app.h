#pragma once

#include "terminal/pty_session.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <memory>
#include <string>
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
  ftxui::Element HomeTile(const std::string& icon,
                          const std::string& name,
                          const std::string& subtitle,
                          bool selected);
  ftxui::Element RunningCard();
  std::string Clock() const;
  std::string HostName() const;

  bool StartSession();
  void ShowApp();
  void ShowHome();

  PtySession& session_;
  ftxui::ScreenInteractive& screen_;
  std::string shell_;
  bool hidden_ = true;
  int selected_tile_ = 0;
  std::string message_;
  ftxui::Element terminal_element_;
};

}  // namespace tdesktop

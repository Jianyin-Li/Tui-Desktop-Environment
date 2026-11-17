#pragma once

#include "terminal/pty_session.h"

#include <ftxui/dom/node.hpp>

namespace tdesktop {

// FTXUI node that paints the PTY screen into the current box.
class TerminalView : public ftxui::Node {
 public:
  explicit TerminalView(PtySession& session);

  void ComputeRequirement() override;
  void SetBox(ftxui::Box box) override;
  void Render(ftxui::Screen& screen) override;

 private:
  PtySession& session_;
  ftxui::Box box_ = {0, 0, 0, 0};
  int rows_ = 0;
  int cols_ = 0;
};

}  // namespace tdesktop

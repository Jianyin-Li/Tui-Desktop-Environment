#include "ui/terminal_view.h"

#include <algorithm>

namespace tdesktop {

TerminalView::TerminalView(PtySession& session) : session_(session) {}

void TerminalView::ComputeRequirement() {
  requirement_.min_x = 1;
  requirement_.min_y = 1;
  requirement_.flex_grow_x = 1;
  requirement_.flex_grow_y = 1;
  requirement_.flex_shrink_x = 1;
  requirement_.flex_shrink_y = 1;
}

void TerminalView::SetBox(ftxui::Box box) {
  box_ = box;
  const int rows = std::max(1, box.y_max - box.y_min + 1);
  const int cols = std::max(1, box.x_max - box.x_min + 1);
  if (rows != rows_ || cols != cols_) {
    rows_ = rows;
    cols_ = cols;
    session_.Resize(rows, cols);
  }
}

void TerminalView::Render(ftxui::Screen& screen) {
  if (box_.x_max < box_.x_min || box_.y_max < box_.y_min) {
    return;
  }
  session_.Render(screen, box_);
}

}  // namespace tdesktop

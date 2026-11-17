#include "test_support.h"

#include <vterm.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

VTermScreenCell CellAt(VTermScreen* screen, int row, int col) {
  VTermScreenCell cell {};
  (void)vterm_screen_get_cell(screen, VTermPos{row, col}, &cell);
  return cell;
}

void Feed(VTerm* vt, VTermScreen* screen, const std::string& bytes) {
  (void)vterm_input_write(vt, bytes.data(), bytes.size());
  vterm_screen_flush_damage(screen);
}

void TestSgrColorIsConsumedNotRendered() {
  VTerm* vt = vterm_new(4, 20);
  CHECK_TRUE(vt != nullptr);
  vterm_set_utf8(vt, 1);
  VTermScreen* screen = vterm_obtain_screen(vt);
  vterm_screen_reset(screen, 1);

  Feed(vt, screen, "\x1b[31mRED\x1b[0m");

  const VTermScreenCell red = CellAt(screen, 0, 0);
  const VTermScreenCell second = CellAt(screen, 0, 1);
  const VTermScreenCell third = CellAt(screen, 0, 2);

  CHECK_EQ(red.chars[0], static_cast<std::uint32_t>('R'));
  CHECK_EQ(second.chars[0], static_cast<std::uint32_t>('E'));
  CHECK_EQ(third.chars[0], static_cast<std::uint32_t>('D'));
  CHECK_TRUE(VTERM_COLOR_IS_INDEXED(&red.fg));
  CHECK_EQ(red.fg.indexed.idx, static_cast<std::uint8_t>(1));

  // Cursor is after "RED"; the CSI bytes must not become visible cells.
  VTermPos cursor {};
  VTermState* state = vterm_obtain_state(vt);
  vterm_state_get_cursorpos(state, &cursor);
  CHECK_EQ(cursor.col, 3);
  CHECK_EQ(cursor.row, 0);

  const VTermScreenCell after = CellAt(screen, 0, 3);
  CHECK_TRUE(after.chars[0] == 0 ||
             after.chars[0] == static_cast<std::uint32_t>(' '));

  vterm_free(vt);
}

void TestBoldAndBackgroundAttributes() {
  VTerm* vt = vterm_new(4, 20);
  CHECK_TRUE(vt != nullptr);
  vterm_set_utf8(vt, 1);
  VTermScreen* screen = vterm_obtain_screen(vt);
  vterm_screen_reset(screen, 1);

  Feed(vt, screen, "\x1b[1;44mBG\x1b[0m");

  const VTermScreenCell cell = CellAt(screen, 0, 0);
  CHECK_EQ(cell.chars[0], static_cast<std::uint32_t>('B'));
  CHECK_TRUE(cell.attrs.bold != 0);
  CHECK_TRUE(VTERM_COLOR_IS_INDEXED(&cell.bg));
  CHECK_EQ(cell.bg.indexed.idx, static_cast<std::uint8_t>(4));

  vterm_free(vt);
}

}  // namespace

int main() {
  TestSgrColorIsConsumedNotRendered();
  TestBoldAndBackgroundAttributes();
  return tdesktop::test::Finish();
}

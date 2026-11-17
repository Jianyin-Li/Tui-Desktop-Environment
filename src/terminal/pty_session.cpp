#include "terminal/pty_session.h"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace tdesktop {
namespace {

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

constexpr std::array<Rgb, 16> kAnsi16 = {{
    {0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
    {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
    {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
    {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
}};

int DistanceSquared(const Rgb& a, const Rgb& b) {
  const int dr = a.r - b.r;
  const int dg = a.g - b.g;
  const int db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

int NearestAnsi16(const Rgb& color) {
  int best = 0;
  int best_distance = DistanceSquared(color, kAnsi16[0]);
  for (int index = 1; index < 16; ++index) {
    const int distance = DistanceSquared(color, kAnsi16[index]);
    if (distance < best_distance) {
      best_distance = distance;
      best = index;
    }
  }
  return best;
}

Rgb Xterm256Rgb(int index) {
  if (index < 16) {
    return kAnsi16[static_cast<std::size_t>(index)];
  }
  if (index < 232) {
    const int offset = index - 16;
    constexpr int levels[6] = {0, 95, 135, 175, 215, 255};
    return {levels[offset / 36], levels[(offset / 6) % 6], levels[offset % 6]};
  }
  const int gray = 8 + (index - 232) * 10;
  return {gray, gray, gray};
}

const std::array<Rgb, 256>& XtermPalette() {
  static const std::array<Rgb, 256> palette = [] {
    std::array<Rgb, 256> result{};
    for (int index = 0; index < 256; ++index) {
      result[static_cast<std::size_t>(index)] = Xterm256Rgb(index);
    }
    return result;
  }();
  return palette;
}

int Nearest256(const Rgb& color) {
  int best = 0;
  int best_distance = DistanceSquared(color, XtermPalette()[0]);
  for (int index = 1; index < 256; ++index) {
    const int distance =
        DistanceSquared(color, XtermPalette()[static_cast<std::size_t>(index)]);
    if (distance < best_distance) {
      best_distance = distance;
      best = index;
    }
  }
  return best;
}

void AppendUtf8(std::uint32_t codepoint, std::string* out) {
  if (codepoint == 0) {
    return;
  }
  if (codepoint < 0x80) {
    out->push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::string CellText(const VTermScreenCell& cell) {
  std::string text;
  for (std::uint32_t codepoint : cell.chars) {
    if (codepoint == 0) {
      break;
    }
    AppendUtf8(codepoint, &text);
  }
  if (text.empty()) {
    text = " ";
  }
  return text;
}

ftxui::Color ToFtxuiColor(const VTermColor& color,
                          ftxui::Terminal::Color support) {
  if (VTERM_COLOR_IS_DEFAULT_FG(&color) || VTERM_COLOR_IS_DEFAULT_BG(&color)) {
    return ftxui::Color::Default;
  }

  if (VTERM_COLOR_IS_INDEXED(&color)) {
    const int index = color.indexed.idx;
    if (index < 16) {
      return ftxui::Color(static_cast<ftxui::Color::Palette16>(index));
    }
    if (support >= ftxui::Terminal::Palette256) {
      return ftxui::Color(static_cast<ftxui::Color::Palette256>(index));
    }
    return ftxui::Color(static_cast<ftxui::Color::Palette16>(
        NearestAnsi16(Xterm256Rgb(index))));
  }

  const Rgb rgb{color.rgb.red, color.rgb.green, color.rgb.blue};
  if (support >= ftxui::Terminal::TrueColor) {
    return ftxui::Color::RGB(static_cast<std::uint8_t>(rgb.r),
                             static_cast<std::uint8_t>(rgb.g),
                             static_cast<std::uint8_t>(rgb.b));
  }
  if (support >= ftxui::Terminal::Palette256) {
    return ftxui::Color(static_cast<ftxui::Color::Palette256>(Nearest256(rgb)));
  }
  return ftxui::Color(static_cast<ftxui::Color::Palette16>(NearestAnsi16(rgb)));
}

ftxui::Screen::Cursor::Shape ToCursorShape(int shape) {
  switch (shape) {
    case VTERM_PROP_CURSORSHAPE_UNDERLINE:
      return ftxui::Screen::Cursor::Underline;
    case VTERM_PROP_CURSORSHAPE_BAR_LEFT:
      return ftxui::Screen::Cursor::Bar;
    case VTERM_PROP_CURSORSHAPE_BLOCK:
    default:
      return ftxui::Screen::Cursor::Block;
  }
}

std::string SanitizeTitle(std::string title) {
  for (char& character : title) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (value < 0x20 || value == 0x7F) {
      character = ' ';
    }
  }
  if (title.size() > 80) {
    title.resize(80);
  }
  return title;
}

}  // namespace

PtySession::~PtySession() {
  Stop();
}

bool PtySession::Start(const std::string& shell,
                       int rows,
                       int cols,
                       std::string* error) {
  if (alive_.load() || master_fd_ >= 0) {
    if (error != nullptr) {
      *error = "session already started";
    }
    return false;
  }

  rows = std::max(1, rows);
  cols = std::max(1, cols);

  struct winsize size {};
  size.ws_row = static_cast<unsigned short>(rows);
  size.ws_col = static_cast<unsigned short>(cols);

  int master = -1;
  const pid_t pid = forkpty(&master, nullptr, nullptr, &size);
  if (pid < 0) {
    if (error != nullptr) {
      *error = std::strerror(errno);
    }
    return false;
  }

  if (pid == 0) {
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    setenv("TERM_PROGRAM", "tdesktop", 1);
    setenv("TDESKTOP", "1", 1);
    const char* program = shell.empty() ? "/bin/sh" : shell.c_str();
    execl(program, program, "-i", static_cast<char*>(nullptr));
    _exit(127);
  }

  master_fd_ = master;
  pid_ = pid;
  rows_ = rows;
  cols_ = cols;
  stopping_.store(false);
  started_at_ = std::chrono::steady_clock::now();

  vt_ = vterm_new(rows, cols);
  if (vt_ == nullptr) {
    if (error != nullptr) {
      *error = "vterm_new failed";
    }
    kill(pid_, SIGKILL);
    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = -1;
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }

  vterm_set_utf8(vt_, 1);
  screen_ = vterm_obtain_screen(vt_);

  static const VTermScreenCallbacks callbacks = [] {
    VTermScreenCallbacks result{};
    result.damage = &PtySession::OnDamage;
    result.movecursor = &PtySession::OnMoveCursor;
    result.settermprop = &PtySession::OnSetTermProp;
    result.bell = &PtySession::OnBell;
    result.resize = &PtySession::OnResize;
    return result;
  }();
  vterm_screen_set_callbacks(screen_, &callbacks, this);
  vterm_screen_enable_altscreen(screen_, 1);
  vterm_screen_reset(screen_, 1);
  vterm_screen_flush_damage(screen_);

  alive_.store(true);
  reader_ = std::thread([this] { ReaderLoop(); });
  return true;
}

void PtySession::Stop() {
  if (master_fd_ < 0 && !reader_.joinable() && pid_ <= 0) {
    CleanupVTerm();
    return;
  }

  stopping_.store(true);
  alive_.store(false);

  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (pid_ > 0 && std::chrono::steady_clock::now() < deadline) {
      int status = 0;
      const pid_t result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_) {
        pid_ = -1;
        break;
      }
      usleep(10 * 1000);
    }
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
  }

  if (master_fd_ >= 0) {
    close(master_fd_);
    master_fd_ = -1;
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  CleanupVTerm();
}

void PtySession::SetScreen(ftxui::ScreenInteractive* screen) {
  ui_screen_.store(screen);
}

void PtySession::WriteInput(std::string_view bytes) {
  if (master_fd_ < 0) {
    return;
  }
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t written =
        write(master_fd_, bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    offset += static_cast<std::size_t>(written);
  }
}

void PtySession::Resize(int rows, int cols) {
  rows = std::max(1, rows);
  cols = std::max(1, cols);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rows == rows_ && cols == cols_) {
      return;
    }
    rows_ = rows;
    cols_ = cols;
    if (vt_ != nullptr) {
      vterm_set_size(vt_, rows, cols);
      if (screen_ != nullptr) {
        vterm_screen_flush_damage(screen_);
      }
    }
  }

  if (master_fd_ >= 0) {
    struct winsize size {};
    size.ws_row = static_cast<unsigned short>(rows);
    size.ws_col = static_cast<unsigned short>(cols);
    ioctl(master_fd_, TIOCSWINSZ, &size);
  }
  NotifyUi();
}

void PtySession::Render(ftxui::Screen& out, ftxui::Box box) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (screen_ == nullptr || vt_ == nullptr) {
    return;
  }

  const int box_rows = box.y_max - box.y_min + 1;
  const int box_cols = box.x_max - box.x_min + 1;
  const int rows = std::min(rows_, box_rows);
  const int cols = std::min(cols_, box_cols);
  const auto support = ftxui::Terminal::ColorSupport();

  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      VTermScreenCell cell {};
      if (vterm_screen_get_cell(screen_, VTermPos{y, x}, &cell) == 0) {
        continue;
      }

      const int width = std::max(1, static_cast<int>(cell.width));
      auto& pixel = out.PixelAt(box.x_min + x, box.y_min + y);
      pixel.character = CellText(cell);

      ftxui::Color foreground = ToFtxuiColor(cell.fg, support);
      ftxui::Color background = ToFtxuiColor(cell.bg, support);
      if (cell.attrs.reverse) {
        std::swap(foreground, background);
      }
      if (cell.attrs.conceal) {
        pixel.character = " ";
      }

      pixel.foreground_color = foreground;
      pixel.background_color = background;
      pixel.bold = cell.attrs.bold != 0;
      pixel.blink = cell.attrs.blink != 0;
      pixel.strikethrough = cell.attrs.strike != 0;
      pixel.underlined = cell.attrs.underline == VTERM_UNDERLINE_SINGLE;
      pixel.underlined_double = cell.attrs.underline == VTERM_UNDERLINE_DOUBLE;

      if (width == 2 && x + 1 < cols) {
        auto& continuation = out.PixelAt(box.x_min + x + 1, box.y_min + y);
        continuation.character = " ";
        continuation.foreground_color = foreground;
        continuation.background_color = background;
        ++x;
      }
    }
  }

  Cursor cursor;
  bool cursor_visible = false;
  {
    cursor = cursor_;
    cursor_visible = cursor_visible_;
  }
  if (cursor_visible && cursor.x >= 0 && cursor.y >= 0 && cursor.x < cols &&
      cursor.y < rows) {
    out.SetCursor({box.x_min + cursor.x, box.y_min + cursor.y,
                   ToCursorShape(cursor.shape)});
  } else {
    out.SetCursor({0, 0, ftxui::Screen::Cursor::Hidden});
  }
}

std::string PtySession::Title() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return SanitizeTitle(title_);
}

PtySession::Cursor PtySession::GetCursor() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Cursor cursor = cursor_;
  cursor.visible = cursor_visible_;
  return cursor;
}

void PtySession::ReaderLoop() {
  std::array<char, 8192> buffer{};

  while (!stopping_.load()) {
    struct pollfd descriptor {};
    descriptor.fd = master_fd_;
    descriptor.events = POLLIN;

    const int polled = poll(&descriptor, 1, 50);
    if (polled < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (polled == 0) {
      continue;
    }

    const ssize_t count = read(master_fd_, buffer.data(), buffer.size());
    if (count > 0) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (vt_ != nullptr) {
          vterm_input_write(vt_, buffer.data(), static_cast<std::size_t>(count));
          if (screen_ != nullptr) {
            vterm_screen_flush_damage(screen_);
          }
        }
      }
      NotifyUi();
      continue;
    }
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    break;
  }

  alive_.store(false);
  NotifyUi();
}

void PtySession::NotifyUi() {
  ftxui::ScreenInteractive* screen = ui_screen_.load();
  if (screen == nullptr) {
    return;
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const std::int64_t now_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  std::int64_t previous = last_post_ns_.load(std::memory_order_relaxed);
  if (now_ns - previous < 16 * 1000 * 1000) {
    return;
  }
  last_post_ns_.store(now_ns, std::memory_order_relaxed);
  screen->PostEvent(ftxui::Event::Custom);
}

void PtySession::CleanupVTerm() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (vt_ != nullptr) {
    vterm_free(vt_);
    vt_ = nullptr;
    screen_ = nullptr;
  }
}

int PtySession::OnDamage(VTermRect /*rect*/, void* /*user*/) {
  return 0;
}

int PtySession::OnMoveCursor(VTermPos pos,
                             VTermPos /*oldpos*/,
                             int visible,
                             void* user) {
  auto* session = static_cast<PtySession*>(user);
  session->cursor_.x = pos.col;
  session->cursor_.y = pos.row;
  session->cursor_.visible = visible != 0;
  session->cursor_visible_ = visible != 0;
  return 0;
}

int PtySession::OnSetTermProp(VTermProp prop, VTermValue* value, void* user) {
  auto* session = static_cast<PtySession*>(user);
  switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
      session->cursor_visible_ = value->boolean != 0;
      session->cursor_.visible = session->cursor_visible_;
      break;
    case VTERM_PROP_CURSORSHAPE:
      session->cursor_.shape = value->number;
      break;
    case VTERM_PROP_TITLE:
      if (value->string.initial) {
        session->title_.clear();
      }
      session->title_.append(value->string.str, value->string.len);
      break;
    default:
      break;
  }
  return 0;
}

int PtySession::OnBell(void* /*user*/) {
  return 0;
}

int PtySession::OnResize(int /*rows*/, int /*cols*/, void* /*user*/) {
  return 0;
}

}  // namespace tdesktop

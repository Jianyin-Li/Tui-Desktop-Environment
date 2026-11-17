#pragma once

#include <vterm.h>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>

namespace tdesktop {

// Owns one PTY and its libvterm screen.
//
// The reader thread is the only writer of the VTerm instance. The UI thread
// renders through Render(), which copies cells under the same mutex. There is
// no scrollback in 0.1: memory stays proportional to the visible screen.
class PtySession {
 public:
  struct Cursor {
    int x = 0;
    int y = 0;
    bool visible = true;
    int shape = VTERM_PROP_CURSORSHAPE_BLOCK;
  };

  PtySession() = default;
  ~PtySession();

  PtySession(const PtySession&) = delete;
  PtySession& operator=(const PtySession&) = delete;

  bool Start(const std::string& shell, int rows, int cols, std::string* error);
  void Stop();

  void SetScreen(ftxui::ScreenInteractive* screen);
  void WriteInput(std::string_view bytes);
  void Resize(int rows, int cols);

  void Render(ftxui::Screen& out, ftxui::Box box);

  bool alive() const { return alive_.load(std::memory_order_relaxed); }
  int pid() const { return pid_; }
  int rows() const { return rows_; }
  int cols() const { return cols_; }
  std::string Title() const;
  Cursor GetCursor() const;

 private:
  static int OnDamage(VTermRect rect, void* user);
  static int OnMoveCursor(VTermPos pos, VTermPos oldpos, int visible,
                          void* user);
  static int OnSetTermProp(VTermProp prop, VTermValue* value, void* user);
  static int OnBell(void* user);
  static int OnResize(int rows, int cols, void* user);

  void ReaderLoop();
  void NotifyUi();
  void CleanupVTerm();

  int master_fd_ = -1;
  pid_t pid_ = -1;
  std::thread reader_;
  std::atomic<bool> alive_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<ftxui::ScreenInteractive*> ui_screen_{nullptr};
  std::atomic<std::int64_t> last_post_ns_{0};

  mutable std::mutex mutex_;
  VTerm* vt_ = nullptr;
  VTermScreen* screen_ = nullptr;
  int rows_ = 0;
  int cols_ = 0;
  std::string title_;
  Cursor cursor_;
  bool cursor_visible_ = true;

  std::chrono::steady_clock::time_point started_at_;
};

}  // namespace tdesktop

# TDESKTOP v1.x 设计：全屏应用 + 隐藏/退出

> 状态：设计草案（待评审）
> 目标版本：1.0.0（单应用全屏）→ 1.1.x（多会话/鼠标/滚动回看）
> 技术栈：C++20 + FTXUI 5.0 + libvterm + forkpty
> **运行环境：Linux TTY（/dev/tty1..63，`TERM=linux`），不是 GUI 里的终端模拟器。**
> 体验融合与实施路线见 [implementation-plan.md](implementation-plan.md)。
> 本文只覆盖 1.x；浮动窗口、工作区、拖拽属于 2.x。

---

## 1. 范围

### 1.1 要做什么

- 在纯终端里提供一个「桌面」：主界面（Home）可以直接启动程序。
- 启动的程序**占满整个终端**，行为与直接运行该 TUI 程序一致（vim、htop、ssh、bash 等）。
- 全局快捷键（默认，均可配置）：
  - **隐藏/恢复**应用（`Alt+Shift+H`）：应用继续在后台运行，桌面回到 Home；再按一次恢复。
  - **退出**当前应用（`Alt+Shift+Q`）：结束进程并回到 Home。
  - **退出桌面**（`Alt+Shift+X`）：结束所有会话并恢复终端。
- 多个应用可同时存在（v1 同一时刻只有一个前台应用，其余处于 hidden）。
- 终端尺寸变化时，应用收到正确的 `SIGWINCH` 与 `TIOCSWINSZ`。

### 1.2 明确不做（v1）

- 浮动/平铺窗口、拖拽、边框、任务栏窗口列表（2.x）。
- 图形程序、X11/Wayland 转发。
- 鼠标事件透传给内部应用（1.1 再做）。
- 滚动回看（scrollback）、复制模式（1.1 再做）。
- PAM 登录 / 多用户（可后续加回，不阻塞 v1）。
- 嵌套会话持久化（桌面退出后应用不保留）。

### 1.3 成功标准（一句话）

> 在 Linux TTY（`TERM=linux`，无 X/Wayland）里运行 `tdesktop`，打开 `vim` 能正常编辑；按隐藏键回到桌面，`vim` 进程仍在；再按恢复键回到原画面；按退出键 `vim` 结束；桌面退出后 TTY 状态完全恢复。

### 1.4 TTY 运行环境的硬约束（重要）

这不是“在 tmux/kitty 里跑”，而是直接占用 Linux 虚拟终端。很多 xterm 假设不成立：

| 项目 | TTY 实际情况 | 设计影响 |
|---|---|---|
| 颜色 | `TERM=linux`，terminfo `colors#8`；FTXUI 最多识别为 16 色 | 强制 16 色主题，禁止 truecolor；libvterm 的 RGB 需降级 |
| alternate screen | `linux` terminfo **没有** `smcup`/`rmcup`，`?1049` 不可依赖 | 不依赖回滚原屏；全屏运行，退出时主动清屏 + 恢复光标/属性 |
| 鼠标 | 控制台没有 xterm SGR 鼠标协议；GPM 是另一套机制 | v1 仅键盘；鼠标放到 1.1（GPM/evdev） |
| 键盘 | 内核控制台 keymap；没有 `Super` 修饰位；Alt 可能是 `ESC` 前缀或第 8 位 | Win 组合无法从 `/dev/tty` 直接获得；Alt 要高/低两种编码都匹配 |
| 功能键 | F1~F12 可能被控制台/VT 占用；`Alt+F1..F12` 直接切 VT | 不把功能键作为默认 |
| Unicode | 需要 UTF-8 locale + 控制台字体；缺字形会花屏 | 提供 ASCII 边框/字符降级开关 |
| 调整大小 | `TIOCGWINSZ`/`SIGWINCH` 可用，但控制台改尺寸依赖 `stty`/`resizefont` | 轮询 + `SIGWINCH` 双保险 |
| 子程序 TERM | libvterm 模拟 xterm，可给子程序 `xterm-256color` | 输出到外层时按 16 色降级 |
| SSH / 串口 | 没有本地 evdev，`TERM` 也不是 `linux` | 不作为 v1 主目标，但检测到非 TTY 时降级不崩溃 |

> v1 的所有 UI 验收都以 `TERM=linux` 的真 TTY 为准；GUI 终端只作为调试环境。

---

## 2. 关键技术结论（先看这一节）

### 2.1 `Super/Win` 收不到、F8~F12 未必有，改用 `Alt+Shift+字母`

这是终端键盘协议和紧凑键盘布局的限制，不是 FTXUI 的问题：

| 组合 | 传统终端实际发送 | 可用性 |
|---|---|---|
| `Ctrl+Q` | `0x11`（C0） | ✅ 但应用常用 |
| `Alt+Q` | `ESC q`（`1b 71`） | ✅ 但 Emacs `M-q` 等常用 |
| `Alt+Shift+Q` | `ESC Q`（`1b 51`） | ✅ 应用很少占用 |
| `Alt+Shift+H` | `ESC H`（`1b 48`） | ✅ 应用很少占用 |
| `Alt+Shift+X` | `ESC X`（`1b 58`） | ✅ 应用很少占用 |
| `Win+Q`（TTY） | 控制台没有 Super 修饰位；通常只产生 `q` 或什么都不发 | ❌ |
| `Win+Q`（GUI 终端） | 通常被窗口管理器/终端吃掉，不发送 | ❌ |
| `Alt+Win+Q` | 两者都不可靠 | ❌（传统协议） |
| `Shift+F12` | `ESC[24;2~`（`1b 5b 32 34 3b 32 7e`） | ⚠️ 仅 GUI 调试；TTY 不推荐 |
| `Ctrl+Alt+Fxx` | 内核可能切换虚拟终端 | ❌ 不要用 |
| kitty 键盘协议（仅 GUI 终端） | `CSI 113;11u`（`1b 5b 31 31 33 3b 31 31 75`） | ⚠️ 纯 TTY 不适用 |

本机实测（FTXUI 5.0.0）：

- 普通字符 `q`：`Event::is_character() == true`，`input() == "q"`。
- `Alt+q`：`Event::is_character() == false`，`Event::input()` 为 `1b 71 00`（末尾带 `\0`）。
- `Alt+Shift+H/Q/X`：在 pty 上分别原样透传 `1b 48` / `1b 51` / `1b 58`；真 TTY 若使用第 8 位模式，需再匹配 `0xC8/0xD1/0xD8`。
- `Alt+Super+q` 的 kitty 序列 `ESC [ 1 1 3 ; 1 1 u`：FTXUI 5.0 **原样透传**，不会解析成按键结构。
- FTXUI 5.0 的 `Event` **没有修饰键 API**，只能读 `event.input()` 的原始字节。

**因此，默认只使用字母键 + 修饰键：**

| 动作 | 默认键 | 原始序列 |
|---|---|---|
| 隐藏 / 恢复应用 | `Alt+Shift+H` | `1b 48` |
| 退出当前应用 | `Alt+Shift+Q` | `1b 51` |
| 退出桌面 | `Alt+Shift+X` | `1b 58` |
| 帮助 | `Alt+?` | `1b 3f` |

理由：

1. **不依赖功能键行**。60%/75% 键盘、部分笔记本没有独立 F8~F12（或需要 `Fn`），字母键在所有键盘上都有。
2. **足够冷门**。加 `Shift` 后得到的是 `ESC` + 大写字母；Emacs/readline 常用的是小写 `M-h` / `M-q`，大写组合默认基本没有绑定，`vim`/`htop`/`bash` 也不占用。
3. **TTY 能编码**。控制台的 Alt 有两种模式：`ESC` 前缀（`1b 48`）或第 8 位（`0xC8`）；KeyRouter 两种都注册，不依赖具体模式。
4. **避开系统键**。不碰 `Super`，也不碰 `Ctrl+Alt+Fxx`（TTY 下会直接切虚拟终端）。

注意事项：

- TTY 下 `Alt+Shift` 不会被桌面环境抢走；但需要同时匹配 `ESC H/Q/X/?` 和 `0xC8/0xD1/0xD8/0xBF` 两套编码。
- 非拉丁键盘布局（俄语、希腊语等）下，`Shift+字母` 产生的字节不一定是 `A-Z`；此时按实际字节配置，或改用 evdev 物理键码方案。
- CapsLock 开启时 `Alt+h` 也会产生 `ESC H`，与 `Alt+Shift+H` 等价。因为绑定本来就是大写组合，行为一致；但文档建议退出类操作不要依赖小写 `Alt+q` 的肌肉记忆。
- 功能键不是默认键；TTY 下不要绑定 `F1~F12` 或 `Alt+Fxx`（可能被控制台占用或直接切 VT）。
- Win 键的可选方案见 2.1.1（evdev / 输入层 remap），v1 默认不启用。

> 实现上使用「原始序列 → 动作」的映射表，而不是 FTXUI 组件里的 `if (event == ...)`。这样 ALT/CTRL/功能键、kitty 序列、未来自定义键位都走同一条路径。

### 2.1.1 TTY 下直接用 Win 键可行吗？

**结论：`/dev/tty` 收不到；TTY 专属的可行路径是 evdev 或输入层 remap。**

| 路径 | 原理 | TTY 可行性 |
|---|---|---|
| Linux TTY `/dev/tty` | 控制台 keymap 只有 Shift/Ctrl/Alt/AltGr，没有 Super 修饰位；`Win+q` 通常只产生 `q` 或什么都不发 | ❌ |
| kitty keyboard protocol | 需要 GUI 终端模拟器；纯 TTY 不适用 | ❌ |
| kitty/WezTerm remap | 终端模拟器功能；纯 TTY 不适用 | ❌ |
| 读 `/dev/input/event*` | 绕过控制台 keymap，直接读键盘设备 | ✅ 可行，需 `input` 组或 udev 授权 |
| 输入层 remap（keyd/kanata/hwdb） | 把 Win 映射成 Alt 或自定义 ESC 序列 | ✅ 可行，需 root/守护进程 |
| X11 grab / portal | 纯 TTY 没有 GUI 会话 | ❌ |

具体障碍：

1. **没有 WM 不等于应用能看到 Super**。TTY 下确实没有窗口管理器抢占，但内核控制台键盘驱动根本不会把 Super 作为修饰键记入 keymap；按键只会在 `KEY_LEFTMETA` 层面被 evdev 看到，不会进入 `/dev/tty` 的字节流。
2. **`loadkeys` 也造不出 Super 修饰位**。控制台 keymap 的修饰位只有 Shift/Ctrl/Alt/AltGr；要改只能在更底层（evdev/hwdb/keyd）把 Win 重映射成 Alt/F13/自定义序列。
3. **evdev 需要权限与过滤**。读 `/dev/input/event*` 通常要 `input` 组或 udev 规则；而且 Win+q 仍会让控制台把 `q` 送进 TTY，必须把多余字符过滤掉，否则会在应用里多打一个 `q`。
4. **SSH/串口不可用**。没有本地输入设备，也没有 Super 的概念。

**因此建议：**

- v1 默认仍用 `Alt+Shift+H/Q/X`，零权限、任何键盘可用；TTY 的 Alt 可能发 `ESC H` 或高位 `0xC8`，KeyRouter 两种都匹配。
- 如果 Win 是硬需求，1.1 增加可选的 `--evdev-hotkeys`：

```
[真实 /dev/tty] ─┐
                 ├─ InputBroker ──▶ FTXUI（stdin 用 pty/pipe 接管）
[/dev/input/*] ──┘
```

  - 用 `poll()` 同时监听 tty 与 evdev；先处理 evdev，Meta 按下时丢弃 tty 中的字符；
  - 用 `EVIOCGKEY` 查询 Meta 状态兜底，避免 evdev/tty 到达顺序竞争；
  - 支持多键盘、热插拔（inotify）、KEY repeat（忽略 value=2）；
  - 不 `EVIOCGRAB`，不独占键盘；失败时自动回退 `Alt+Shift`；
  - 需要 `input` 组或 udev 规则；默认关闭，配置文件显式开启。

- 另一种更省事的方式是安装 `keyd`/`kanata`，在输入层把 `Win+q/h/x` 映射成 `ESC Q`/`ESC H`/`ESC X`，TDESKTOP 无需改代码：

```conf
# keyd 示例（需要 root 服务；语法以你的版本为准）
[meta]
q = macro(esc, shift-q)
h = macro(esc, shift-h)
x = macro(esc, shift-x)
```

  - 优点：不动 TDESKTOP 的输入路径；
  - 缺点：系统级配置、需要 root、会影响所有 TTY/应用，必须带上“退出/崩溃后恢复”的安装脚本。

> 结论：Win 键在 TTY 上**不是完全不可能**，但它属于“输入层改造”，不是普通 `/dev/tty` 应用能直接实现的功能。v1 不把它作为默认；如果你确认要走 evdev 路线，我可以把它排进 1.1 的 M6。

### 2.2 为什么不能「让子进程直接接管终端」

最简单做法是：启动时退出/挂起 FTXUI，把 tty 直接交给子进程，程序退出后再恢复桌面。它确实能 100% 还原应用画面，但有一个致命问题：

- 子进程运行期间，键盘输入全部属于子进程，**桌面的全局快捷键不可能收到**，因此「隐藏」「退出」无法由桌面拦截；
- 一旦隐藏，桌面进程也拿不回屏幕内容。

所以 1.x 必须采用 **PTY + 终端模拟**：子进程始终以为自己在跟一个真实终端通信，桌面进程负责解析它的输出、绘制到 FTXUI，并把按键转发回去。

### 2.3 终端模拟后端选型

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| **libvterm** | 成熟（vim/tmux 同款解析器），内层支持 alt-screen、wide char、256 色/truecolor、光标、标题、鼠标协议；外层再降级 | 多一个 C 依赖（`libvterm-dev`） | ✅ **v1 采用** |
| 自写 VT 解析器 | 无依赖 | vim/less/htop 的转义序列非常杂，至少数千行才能稳定 | ❌ 不划算 |
| tmux 后端 | 保真度最高、开发最快 | 依赖 tmux；`capture-pane` 轮询有延迟；输入注入编码麻烦；嵌套 tmux 环境 | ⚠️ 仅作为应急 fallback |
| 直接 handoff | 零模拟成本 | 无法隐藏/全局拦截，见 2.2 | ❌ |

`libvterm` 当前未安装，构建前需要：

```sh
sudo apt install libvterm-dev
```

CMake 侧同时兼容 pkg-config 与手动查找（不同发行版包名可能是 `vterm` / `libvterm`）：

```cmake
find_package(ftxui CONFIG REQUIRED)
find_path(VTERM_INCLUDE_DIR vterm.h REQUIRED)
find_library(VTERM_LIBRARY NAMES vterm libvterm REQUIRED)
target_link_libraries(tdesktop PRIVATE ftxui::component ${VTERM_LIBRARY})
```

### 2.4 FTXUI 5.0 能力核对

| 能力 | API | TTY 下的用法 |
|---|---|---|
| 全屏 | `ScreenInteractive::Fullscreen()` | 发送 `?1049h`，但 `TERM=linux` 的 terminfo 没有 `smcup`；需验证并准备“手动清屏 + 退出清理”兜底 |
| 自定义绘制节点 | 继承 `ftxui::Node`，重写 `Render(Screen&)` | 把终端网格画到 FTXUI 的 `Pixel` |
| 单元格样式 | `Pixel.character/foreground_color/background_color/bold/...` | 只使用 16 色；RGB 先降级 |
| 光标 | `Screen::SetCursor({x,y,shape})` | 应用光标位置 |
| 跨线程唤醒 | `Screen::PostEvent(Event::Custom)` | PTY 线程通知 UI 重绘 |
| 临时恢复 tty | `Screen::WithRestoredIO(closure)` | 兜底/调试/外部编辑器 |
| 全局事件拦截 | `CatchEvent(handler)` 在子组件之前执行 | 快捷键优先于应用 |
| 颜色能力 | `Terminal::ColorSupport()` | 实测 `TERM=linux` → `Palette16`；强制覆盖为 16 色 |
| 鼠标 | `TrackMouse(false)` | TTY 无 SGR 鼠标，必须关闭 |

> `Fullscreen()` 的 alternate screen 是 **xterm 概念**；Linux 控制台不支持时，不要假设退出后能自动恢复原画面。TTY 模式需要自己记录并恢复：清屏、光标显示、SGR 重置、键盘模式。详见 6.4。

---

## 3. 总体架构

```
 /dev/tty ──────────────┐
 /dev/input (可选) ─────┤ InputBroker ──▶ 原始字节/动作
                        ▼
                 ┌──────────────────────────────────────┐
                 │           DesktopApp                 │
                 │  ScreenInteractive (TTY 模式)         │
                 │                                      │
                 │  KeyRouter ──▶ HomeView / AppView    │
                 │      │             │                 │
                 │      │             ▼                 │
                 │      │        VTermNode (Node)        │
                 │      │             │ snapshot         │
                 │      ▼             ▼                 │
                 │  TerminalSession (id, state, pty)    │
                 │      │  ▲                            │
                 └──────┼──┼────────────────────────────┘
                        │  │ read() bytes
             write()    │  │
                        ▼  │
                 ┌──────────────┐
                 │  forkpty()   │
                 │  child app   │  ◀── vim / htop / bash
                 │  (own pgrp)  │
                 └──────────────┘
```

### 3.1 模块划分

| 模块 | 职责 |
|---|---|
| `app::DesktopApp` | 根组件：视图状态、全局事件分发、Toast/确认框 |
| `app::KeyRouter` | 原始序列 → `Action`；可配置键位 |
| `app::AppRegistry` | 扫描 `.desktop` 与内置命令，生成可启动项 |
| `app::HomeView` | 桌面主界面：应用列表、运行中会话、快捷键提示 |
| `app::AppView` | 全屏应用视图：`VTermNode` + 可选浮层 |
| `tty::ConsoleAdapter` | TTY 检测、16 色/ASCII 降级、全屏/退出恢复、VT 保护 |
| `input::InputBroker` | 统一 tty 与 evdev 输入；过滤 Win 组合产生的杂散字符（1.1 可选） |
| `input::EvdevHotkeys` | 监听 `/dev/input`，实现真实 `Win+q/h/x`（1.1 可选） |
| `term::TerminalSession` | 一个 PTY 会话：forkpty、reader 线程、resize、stop、reap |
| `term::VtScreen` | libvterm 封装：喂入字节、取单元格快照、光标/标题/鼠标模式 |
| `term::VTermNode` | FTXUI 自定义 `Node`，把快照绘制到 `Screen` |
| `ui::Theme` | 颜色、边框、提示样式 |
| `util::Config` | INI 配置与键位解析 |
| `util::Log` | 可选日志（默认关闭） |

目录建议：

```
CMakeLists.txt
src/main.cpp
src/app/desktop_app.{h,cpp}
src/app/key_router.{h,cpp}
src/app/app_registry.{h,cpp}
src/app/home_view.{h,cpp}
src/app/app_view.{h,cpp}
src/term/terminal_session.{h,cpp}
src/term/vt_screen.{h,cpp}
src/term/vterm_node.{h,cpp}
src/term/pty.{h,cpp}
src/tty/console_adapter.{h,cpp}
src/input/input_broker.{h,cpp}
src/input/evdev_hotkeys.{h,cpp}
src/ui/theme.{h,cpp}
src/ui/toast.{h,cpp}
src/util/config.{h,cpp}
src/util/log.{h,cpp}
tests/...
```

### 3.2 线程模型

只允许两个线程碰终端：

1. **UI 线程**（FTXUI 主循环）：读取 `TerminalSession` 的网格快照、处理按键、决定视图。
2. **每个会话一个 reader 线程**：阻塞 `read(master_fd)`，把字节喂给 libvterm；标记 dirty 后 `PostEvent(Event::Custom)`。
3. **InputBroker 线程**（仅 `--evdev-hotkeys` 开启）：`poll()` 真实 tty 与 evdev，过滤 Win 组合的多余字符，把普通字节写入 FTXUI 的 stdin pty/pipe；动作通过线程安全的 action 队列 + `PostEvent` 交给 UI。

约束：

- libvterm **不是线程安全的**。`feed()` 与 `snapshot()` 共用一把 `std::mutex`；UI 线程只在渲染瞬间加锁，拷贝当前可见网格。
- 不要每个字节都 `PostEvent`。用 `std::atomic<bool> dirty_` + 最多 60Hz 的通知节流：

```cpp
void TerminalSession::notify() {
  bool expected = false;
  if (!dirty_.compare_exchange_strong(expected, true)) return;
  auto now = steady_clock::now();
  if (now - last_post_ < 16ms) return;   // 由 UI 的 16ms tick 兜底
  last_post_ = now;
  screen_->PostEvent(Event::Custom);
}
```

- 应用输出洪水（`yes`、大文件 `cat`）时只丢「重绘帧」，不丢「字节」；reader 永远全速消费，避免 PTY 缓冲区反压到子进程。
- 隐藏状态下仍然消费输出（否则应用会卡在 `write`），只是不触发重绘。

### 3.3 视图状态机

```
                 ┌──────────── 启动 ────────────┐
                 ▼                              │
             ┌───────┐  Alt+Shift+X              │
             │ Home  │ ───────────────────────▶ 退出桌面
             └───┬───┘
       Enter 启动│  ▲ Alt+Shift+H（有 hidden 会话）
                 ▼  │
        ┌──────────────────┐
        │ AppForeground    │
        │  (fullscreen)    │
        └───┬──────────┬───┘
 Alt+Shift+H│          │ Alt+Shift+Q
            ▼          ▼
     ┌───────────┐  ┌──────────────┐
     │ Home +    │  │ 结束会话      │
     │ hidden 会话│  │ 回到 Home     │
     └───────────┘  └──────────────┘
```

- `Alt+Shift+H` 是 toggle：前台 → 隐藏回 Home；Home 且存在最近会话 → 恢复。
- `Alt+Shift+Q` 只作用于「当前聚焦会话」：前台应用直接结束；Home 上选中的运行中会话也可结束。
- 退出桌面使用独立组合（默认 `Alt+Shift+X`），避免误触；若有运行中会话，弹确认框。

---

## 4. 交互设计

### 4.1 Home 界面（v1）

```
┌ TDESKTOP ─────────────────────────────────────────── 10:24 ─┐
│                                                             │
│  应用                                                        │
│  ▸ Shell (bash)                                             │
│    htop                                                     │
│    vim                                                      │
│    python3                                                  │
│                                                             │
│  运行中                                                      │
│    ● vim  ~/src/main.cpp        Enter 恢复   x 结束          │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ ↑↓ 选择  Enter 打开  Alt+Shift+H 隐藏/恢复                  │
│ Alt+Shift+Q 退出  Alt+Shift+X 退出桌面  Alt+? 帮助          │
└─────────────────────────────────────────────────────────────┘
```

- v1 用纵向列表，不做图标网格（终端里图标字体不可靠）。
- 顶部一行：主机名 + 时钟；底部一行：按键提示。
- 应用来源：
  1. `~/.config/tdesktop/config.ini` 中的 `[app.xxx]` 段；
  2. `/usr/share/applications/*.desktop` 中 `Terminal=true` 或 `Categories=System;TerminalEmulator;` 的条目（v1.1 再做完整 Exec 解析）；
  3. 内置项：`$SHELL`、`htop`、`vim`、`python3`（存在才显示）。

```ini
[app.shell]
name = Shell
cmd  = /bin/bash

[app.vim]
name = vim
cmd  = vim

[app.htop]
name = htop
cmd  = htop
```
- 输入过滤：Home 上直接打字即搜索（复用 FTXUI `Input`）。

### 4.2 全屏应用界面

- 默认 **0 装饰**：应用内容 = 整个屏幕，最接近「全屏打开程序」。
- 启动/隐藏/恢复时显示 1.5 秒 Toast（不遮挡输入）：
  - `已打开 vim · Alt+Shift+H 隐藏 · Alt+Shift+Q 退出`
  - `vim 已在后台运行 · Alt+Shift+H 恢复`
- 可选配置 `show_status_bar = true`：顶部保留 1 行（应用名 + 时间 + 快捷键），其余给应用。默认关闭。

### 4.3 默认键位

> 选键原则：**所有键盘都有 + 终端能可靠编码 + 终端应用里不常用 + 不触发系统/终端自身快捷键**。
> 因此不用功能键（部分键盘没有 F8~F12）、不用 `Alt+Q/H`（vim/Emacs/readline 里太常见）、也不用 `Ctrl+Alt+Fxx`（Linux 会切 TTY）。

| 动作 | 默认键 | 原始序列（ESC / 第8位） | 说明 |
|---|---|---|---|
| 隐藏/恢复 | `Alt+Shift+H` | `1b 48` / `0xC8` | H = Hide，最常用 |
| 退出当前应用 | `Alt+Shift+Q` | `1b 51` / `0xD1` | Q = Quit |
| 退出桌面 | `Alt+Shift+X` | `1b 58` / `0xD8` | X = eXit，有会话时二次确认 |
| 帮助 | `Alt+?` | `1b 3f` / `0xBF` | 物理键为 `Alt+Shift+/` |
| Home 选择 | `↑/↓` | 原始序列 | 只在 Home 生效 |
| Home 打开 | `Enter` | `0a` | |
| Home 结束会话 | `x` | `78` | 只在「运行中」列表聚焦时 |

> TTY 的 Alt 可能是 `ESC` 前缀，也可能是第 8 位（`0x80` 置位）；KeyRouter 对每个逻辑键注册两种序列。

配置示例：

```ini
[keys]
hide       = alt+shift+h
exit_app   = alt+shift+q
quit       = alt+shift+x
help       = alt+?

[general]
show_status_bar = false
startup        = shell      ; 启动后直接进入的应用；留空则显示 Home
```

可选方案（**1.1 预留，TTY 专属**）：通过 evdev Broker 或 `keyd`/`kanata` 输入层 remap 实现真实 Win 键。

```ini
[keys]
hide     = super+h
exit_app = super+q
quit     = super+x
help     = alt+?
```

`super` 组合无法从 `/dev/tty` 直接获得；必须由 2.1.1 描述的 evdev Broker 或系统级 remap 产生动作。v1 不启用这条路径，默认回退到 `Alt+Shift`。

解析规则：

- 名字大小写不敏感，分隔符 `+`；支持 `ctrl`、`alt`、`shift`、`super`（super 需要 evdev/remap）。
- 支持常见别名：`slash` = `/`、`question` = `?`、`comma`、`period` 等。
- 内部统一转成原始字节串；`alt+shift+h` → `{1b 48, C8}` 两个等价序列。
- 匹配时先去掉 FTXUI 追加的尾部 `\0`，再做**完全相等**比较，避免误吞应用按键。
- 对同一逻辑键注册多个等价序列（Alt 的 ESC/高位两种编码），解决不同 TTY/终端差异。
- 配置中的 `super` 只在 evdev Broker 或输入层 remap 启用时生效；v1 不把它作为默认。

### 4.4 鼠标（v1 不做）

- Linux TTY 没有 xterm SGR 鼠标协议；`TrackMouse(false)` 必须关闭，否则可能向控制台写入无效序列。
- 1.1 再做：GPM（`/dev/gpmdata`）或 evdev 读取鼠标，映射为应用可理解的 SGR 序列写回内部 PTY。
- 内部应用的 `DECSET 1000/1002/1003/1006` 先记录下来，供 1.1 使用。

---

## 5. 核心接口草案

### 5.1 键位路由

```cpp
enum class Action { None, ExitApp, ToggleHide, QuitDesktop, Help, ... };

class KeyRouter {
 public:
  bool Load(const Config& cfg, std::string* err);
  std::optional<Action> Match(const ftxui::Event& e) const;

 private:
  // 原始字节串 -> 动作
  std::unordered_map<std::string, Action> map_;
  static std::string Normalize(ftxui::Event e);  // 去掉尾部 '\0'
};
```

根组件里先匹配全局键，再决定是否转发：

```cpp
auto root = ftxui::CatchEvent(view, [&](ftxui::Event e) {
  if (auto action = keys_.Match(e)) {
    HandleAction(*action);
    return true;                 // 不再传给应用
  }
  if (mode_ == Mode::AppForeground) {
    if (e.is_mouse()) return false;       // v1 不转发鼠标
    active_->WriteInput(KeyRouter::Normalize(e));
    return true;
  }
  return false;                  // 交给 HomeView / 输入框
});
```

> 注意：应用内的 `Ctrl+C`、`Ctrl+Z`、`Esc` 必须原样转发，不能被桌面拦截。只有显式配置的组合才拦截。

### 5.2 PTY 会话

```cpp
class TerminalSession {
 public:
  bool Start(const std::vector<std::string>& argv, int rows, int cols,
             ftxui::ScreenInteractive* screen, std::string* err);
  void WriteInput(std::string_view bytes);
  void Resize(int rows, int cols);          // TIOCSWINSZ + vterm_set_size
  void RequestStop();                       // 异步：SIGTERM → 2s → SIGKILL
  void Snapshot(GridSnapshot* out);         // 加锁拷贝可见网格 + 光标
  bool Alive() const;
  int  Pid() const;
  const std::string& Title() const;         // 来自 OSC 0/2
  ~TerminalSession();                       // 保证 reap + close(fd)
};
```

`Start` 的实现要点：

```cpp
struct winsize ws{ .ws_row = rows, .ws_col = cols, .ws_xpixel = 0, .ws_ypixel = 0 };
pid_t pid = forkpty(&master_fd_, nullptr, nullptr, &ws);
if (pid == 0) {
  // 子进程：forkpty 已 setsid + 把 slave 设为控制终端
  setenv("TERM", "xterm-256color", 1);
  setenv("COLORTERM", "truecolor", 1);
  setenv("TERM_PROGRAM", "tdesktop", 1);
  unsetenv("TMUX"); unsetenv("TMUX_PANE");
  execvp(argv[0].c_str(), const_cast<char* const*>(argv_ptr));
  _exit(127);
}
// 父进程：vterm_new(rows, cols) + reader 线程
```

- 子进程天然拥有独立进程组（`forkpty` → `setsid`），停止时 `kill(-pid, SIGTERM)` 能带走它派生的子进程。
- `read()` 返回 `0` / `EIO` 表示 slave 已关闭：调用 `waitpid(pid, &status, 0)` reap，标记 `alive_ = false`。
- `RequestStop()` 由 UI 线程发起，实际等待放到小线程或 100ms tick 里，绝不能阻塞渲染。
- 桌面退出时对**所有**会话执行 `RequestStop()`，最后再 `screen.Exit()`。

### 5.3 libvterm 封装

```cpp
class VtScreen {
 public:
  VtScreen(int rows, int cols);            // vterm_new + obtain_screen + reset
  void Feed(const char* data, size_t len); // vterm_input_write + flush_damage
  void Resize(int rows, int cols);         // vterm_set_size
  void CopyVisible(std::vector<Cell>* out, CursorInfo* cursor) const;
  const std::string& Title() const;
  bool BracketedPaste() const;
  bool MouseEnabled() const;               // 1.1 使用
 private:
  VTerm* vt_ = nullptr;
  VTermScreen* screen_ = nullptr;
  std::string title_;
};
```

单元格映射：

| libvterm | FTXUI / TTY |
|---|---|
| `VTermScreenCell.chars[0]` | `Pixel.character`（UTF-8 编码，宽字符 `width==2` 时下一格留空） |
| `cell.fg/bg`（indexed/RGB/default） | TTY 模式统一降级到 `Color::Palette16`；`Color::RGB` 只用于 GUI 调试 |
| `attrs.bold/underline/italic/blink/reverse/strike` | `Pixel.bold/underlined/...`，`reverse` 交换前景背景；Linux 控制台不支持斜体/删除线时忽略 |
| `VTermPos cursor` + `CURSORVISIBLE` | `Screen::SetCursor` |

> TTY 是 8/16 色环境：libvterm 的 256 色/RGB 必须映射到最近的 16 色；`TERM=linux` 下不要输出 truecolor 序列。宽字符/emoji 若控制台字体缺字形，降级为 `?` 或 ASCII 占位。

`settermprop` 回调用于保存标题、光标可见性、光标形状；`bell` 回调可用于响铃/闪烁提示（可选）。

### 5.4 绘制节点

```cpp
class VTermNode : public ftxui::Node {
 public:
  explicit VTermNode(TerminalSession& s) : session_(s) {}

  void ComputeRequirement() override {
    requirement_.min_x = 20;
    requirement_.min_y = 5;
  }
  void SetBox(ftxui::Box box) override { box_ = box; }

  void Render(ftxui::Screen& screen) override {
    GridSnapshot snap;
    session_.Snapshot(&snap);

    int rows = std::min(box_.y_max - box_.y_min + 1, (int)snap.rows);
    int cols = std::min(box_.x_max - box_.x_min + 1, (int)snap.cols);
    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < cols; ++x) {
        auto& p = screen.PixelAt(box_.x_min + x, box_.y_min + y);
        const Cell& c = snap.at(x, y);
        p.character = c.text;
        p.foreground_color = c.fg;
        p.background_color = c.bg;
        p.bold = c.bold;
        p.underlined = c.underline;
        // ...
      }
    }
    if (snap.cursor_visible) {
      screen.SetCursor({ box_.x_min + snap.cursor_x,
                         box_.y_min + snap.cursor_y,
                         ftxui::Screen::Cursor::Block });
    }
  }
};
```

性能：200×50 = 10,000 格，全量拷贝 + 绘制在 60fps 下没有压力（FTXUI 本身就是逐格写 `Pixel`）。如果后续出现卡顿，再改用 libvterm 的 `damage` 回调只重绘脏矩形。

### 5.5 主循环

```cpp
int main() {
  Config cfg = Config::Load();
  AppRegistry apps = AppRegistry::Scan();

  tty::ConsoleAdapter console;      // 检测 TERM=linux，保存 termios/颜色/光标状态
  auto screen = console.CreateScreen();  // TTY: FixedSize；GUI: Fullscreen
  screen.TrackMouse(false);         // TTY 无鼠标；GUI 调试也先关闭

  DesktopApp desktop(screen, cfg, std::move(apps));
  desktop.Start();                  // 按配置启动默认应用

  std::signal(SIGWINCH, on_winch);  // 由 FTXUI/ConsoleAdapter 处理重排

  screen.Loop(desktop.Root());
  desktop.Shutdown();               // stop all sessions + reap
  console.Shutdown();               // 清屏、显示光标、恢复 termios
  return 0;
}
```

### 5.6 ConsoleAdapter（TTY 适配）

```cpp
class ConsoleAdapter {
 public:
  bool IsTty() const;                    // TERM=linux 且 isatty(0)
  ftxui::ScreenInteractive CreateScreen();// TTY: FixedSize(cols,rows); GUI: Fullscreen
  void ApplyCaps();                      // Palette16, locale, ASCII/Unicode 选择
  void Shutdown();                       // 清屏 + 光标 + termios 恢复
};

// ApplyCaps() 关键动作：
//   Terminal::SetColorSupport(Terminal::Palette16);  // 忽略 COLORTERM
//   locale 非 UTF-8 或 --ascii 时，使用 ASCII 边框
//   rows/cols = ioctl(STDOUT_FILENO, TIOCGWINSZ)
```

- `TERM=linux` 时不要把 `COLORTERM=truecolor` 当能力；实测 FTXUI 会因此误判为 TrueColor（`Terminal::ColorSupport()` 返回 3），必须手动覆盖。
- `SIGWINCH` 时重新读取尺寸并重建 `ScreenInteractive`；重建前保存 Home/App 视图状态。
- 如果 stdout 不是 tty（重定向/日志），退回 `TerminalOutput` 或纯文本模式，避免输出转义垃圾。

---

## 6. 生命周期与健壮性

### 6.1 启动

1. 读配置（不存在则用默认值并生成模板）。
2. 扫描应用列表。
3. 检测运行环境：`TERM=linux` / `ttyname(0)` 为 `/dev/tty*` → 进入 **TTY 模式**（16 色、无鼠标、无 alt-screen 假设）；否则走 GUI 终端调试模式。
4. TTY 模式优先使用 `ScreenInteractive::FixedSize(cols, rows)`（不发送 `?1049`）；GUI 模式用 `Fullscreen()`。尺寸变化时重建 screen。
5. 若有 `startup`，创建会话并进入 `AppForeground`；否则显示 Home。
6. 保存 termios、光标、颜色状态；安装信号处理：`SIGINT/SIGTERM/SIGHUP/SIGQUIT` 恢复终端后退出；`SIGWINCH` 交给 FTXUI/ConsoleAdapter + 触发会话 resize。

### 6.2 隐藏 / 恢复

- **隐藏**：仅切换视图状态；不向子进程发任何信号，不暂停 reader。
- 应用在后台继续运行，输出继续写入 vterm 的屏幕缓冲（不保留 scrollback）。
- **恢复**：切回 `AppForeground`，用最新快照重绘；随后发送一次 `Custom` 事件确保立即刷新。
- 若隐藏期间应用退出：Home 的「运行中」列表移除该会话，并弹出 Toast `vim 已退出（状态 0）`。

### 6.3 退出应用

```
Alt+Shift+Q
  → RequestStop()
  → kill(-pid, SIGTERM)
  → 每 50ms waitpid(WNOHANG)，最多 2s
  → 仍存活则 kill(-pid, SIGKILL)
  → waitpid 回收
  → close(master_fd)，join reader 线程
  → 会话从 map 移除，回到 Home
```

- 结束的是**整个进程组**，避免 `bash -c 'vim'` 之类残留。
- 退出桌面时逐个执行上述流程；若超过 5 秒仍未清空，打印警告后仍然退出（不能再把终端卡住）。
- 析构函数中兜底：`kill(SIGKILL)` + `waitpid`，避免僵尸。

### 6.4 终端恢复（TTY 下最重要）

必须保证以下路径都能恢复：正常退出（应用自行退出）、`Alt+Shift+X`、`SIGINT`、`SIGTERM`、未捕获异常。

- **不要依赖 alternate screen**。`TERM=linux` 没有 `smcup/rmcup`；TTY 模式退出时由 `ConsoleAdapter` 主动清屏、显示光标、重置 SGR。
- 正常路径：FTXUI 析构后，`ConsoleAdapter::Shutdown()` 执行：

```cpp
// 终端恢复到可用状态；不清除 scrollback 以外的用户数据
static const char reset_seq[] = "\x1b[0m\x1b[?25h\x1b[2J\x1b[H";
write(STDOUT_FILENO, reset_seq, sizeof(reset_seq) - 1);
tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios_);  // 恢复行缓冲/回显
```

- 信号路径：注册 `sigaction`，handler 中只做 async-signal-safe 操作：

```cpp
static struct termios g_saved_termios;
static void restore_and_die(int sig) {
  const char seq[] = "\x1b[0m\x1b[?25h\x1b[2J\x1b[H";
  (void)!write(STDOUT_FILENO, seq, sizeof(seq) - 1);
  tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);  // 关键：否则 TTY 仍是 raw
  _exit(128 + sig);
}
```

- 严格说 `tcsetattr` 不在所有 POSIX 实现的 async-signal-safe 列表里；Linux 实际可用。更稳妥的方案是 self-pipe：handler 只 `write(pipe, ...)`，主循环收到后走正常恢复流程。

- `SIGSEGV` 等致命信号无法完全保证，但至少注册 `std::set_terminate` 做一次恢复尝试。
- 若启用了 evdev 输入代理，不要修改控制台 keymap；如做过输入层 remap，必须由安装脚本/`systemd ExecStopPost` 恢复。
- 提供 `--screen=auto|tty|gui`（或 `--no-alt-screen`）调试开关；默认 auto。

### 6.5 尺寸变化

- FTXUI 自己处理 `SIGWINCH` 并重排；`VTermNode::SetBox` 得到新的可用区域。
- 会话层比较 `(rows, cols)`，变化时：
  1. `ioctl(master_fd, TIOCSWINSZ, &ws)` —— 内核会给前台进程组发 `SIGWINCH`；
  2. `vterm_set_size(vt_, rows, cols)`，清空/裁剪快照。
- 去抖：连续 resize 时以 50ms 内的最后一次为准，避免 vim 反复重排。

### 6.6 输出洪水与延迟

- reader 使用 8KB 缓冲循环读，直到 `EAGAIN`（fd 设为非阻塞）或读到 0。
- UI 重绘最多 60fps；隐藏时不重绘。
- 不在 reader 线程里做颜色转换；快照只拷贝字符与颜色枚举，绘制在 UI 线程完成。

---

## 7. 启动模式与系统集成

目标：既能**手动运行**，也能**安装成可选会话**；默认不碰 `graphical.target`、不碰 GDM/SDDM/LightDM，不影响现有 GUI。

### 7.1 模式一览

| 模式 | 启动方式 | 适用场景 | 是否影响 GUI |
|---|---|---|---|
| **手动** | 在当前 TTY 执行 `tdesktop` | 开发、临时使用、救援 | 否 |
| **加法 target** | `systemctl start tdesktop.target` 或 enable 后开机自启 | 在备用 VT 常驻一个 TUI 桌面，GUI 继续运行 | 否（只占用指定 VT） |
| **独占 target** | `systemctl isolate tdesktop-session.target` / `set-default` | 把这台机器当纯 TTY 桌面用 | 仅在选择独占时停止 GUI |
| **登录管理器**（1.2） | `tdesktop-greeter.service` | 类似 GDM 的 PAM 登录界面 | 否/可配置 |

> “desktop manager” 有两种常见含义：**桌面会话**（像 GNOME Session）和**登录管理器**（像 GDM）。按你说的“额外注册一个 target、不影响 GUI”，下面以**加法会话 target**为主；PAM greeter 单列为 1.2。

### 7.2 TTY 选择与共存原则

- 默认使用 **`tty12`**（可配置），避开 GUI/显示管理器常占用的 `tty1`/`tty7`，以及 getty 的 `tty1~6`。
- 安装前检查：`/sys/class/tty/tty0/active`、`systemctl status getty@tty12`、`loginctl seat-status`、`fuser /dev/tty12`；被占用则拒绝安装或要求 `--force`。
- 只停止/禁用 **tty12 上的 getty**，不停止其他 getty，不重启 display-manager。
- 卸载/停止 target 时恢复 tty12 的 getty，避免留下一个死 VT。
- GUI 与 TUI 可同时存在：`Ctrl+Alt+F7` 回 GUI，`Ctrl+Alt+F12` 回 TUI；`Alt+F1..F12` 始终由内核处理。
- **evdev Win 热键必须判断当前活动 VT**：只有 `/sys/class/tty/tty0/active` 等于 TDESKTOP 的 VT 时才触发；否则用户在 GUI 里按 `Win+q` 会误关 TUI 应用。TTY 标准输入本身只在该 VT 活动时到达，不需要这个判断；evdev 需要。

### 7.3 加法 target（默认方案）

unit 安装到 `/usr/local/lib/systemd/system/`（本地安装）或发行包放到 `/usr/lib/systemd/system/`：

`tdesktop.service`：

```ini
[Unit]
Description=TDESKTOP TUI Desktop
Documentation=man:tdesktop(1)
PartOf=tdesktop.target
After=systemd-user-sessions.service
# 只与 tty12 的 getty 冲突，不影响其他 VT/GUI
Conflicts=getty@tty12.service
# 可选：display-manager 仍在运行，不写 Conflicts

[Service]
Type=simple
# 安装器把 YOURUSER 替换为实际用户；不要用 %u（系统级 service 的 %u 是 root）
User=YOURUSER
RuntimeDirectory=tdesktop
PAMName=login
TTYPath=/dev/tty12
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=no
StandardInput=tty
StandardOutput=tty
ExecStart=/usr/bin/tdesktop-manager --session
# 退出后把 tty12 还给登录提示；失败不无限重启
ExecStopPost=-/bin/systemctl --no-block start getty@tty12.service
Restart=no
```

> `tdesktop-manager` **只被 target 使用**：它负责 PAM/logind 会话、环境合成，然后 `exec` `tdesktop --session`。
> 命令行直接运行 `tdesktop` 时不启动 manager，也不创建 PAM 会话，完整继承调用者的 shell 身份/环境（详见 implementation-plan.md 3.5）。
> manager 通过 envp 传递环境，不把环境变量放进命令行参数。

`tdesktop.target`：

```ini
[Unit]
Description=TDESKTOP TUI Desktop (additive)
Wants=tdesktop.service
After=tdesktop.service
AllowIsolate=no

[Install]
WantedBy=multi-user.target
```

使用：

```sh
# 按需启动/停止（GUI 不动）
sudo systemctl start tdesktop.target
sudo systemctl stop  tdesktop.target

# 开机自动启动（仍与 GUI 共存，占 tty12）
sudo systemctl enable tdesktop.target

# 完全卸载
sudo tdesktop-install --uninstall
```

要点：

- `WantedBy=multi-user.target` 只是把 target 挂到多用户阶段，**不会**停止 `graphical.target`。
- `PartOf=tdesktop.target` 保证 `systemctl stop tdesktop.target` 会一起停止 service，不会留下孤儿进程。
- `Conflicts=getty@tty12.service` 是唯一的“侵入点”，且只影响一个 VT。
- 如果不希望开机启动，**不要 enable**，只用 `systemctl start` 手动拉起。
- `PAMName=login` 让 systemd 通过 PAM 建立 logind 会话（`loginctl` 能看到、`XDG_RUNTIME_DIR`/seat 正常）；需要验证发行版的 PAM 栈。若失败，退回 7.5 的 getty autologin 方案。

### 7.4 独占 TUI target（可选）

给“这台机器就是 TTY 桌面”的用户；**默认不启用**，必须显式选择：

`tdesktop-session.target`：

```ini
[Unit]
Description=TDESKTOP Exclusive Session
Requires=multi-user.target
Wants=tdesktop.service
Conflicts=graphical.target
After=multi-user.target
AllowIsolate=yes
```

选择方式（三选一）：

```sh
# 1) 临时从当前 TTY 切换（会停止 GUI）
sudo systemctl isolate tdesktop-session.target

# 2) 永久默认（会改变启动目标；可回滚）
sudo systemctl set-default tdesktop-session.target
sudo systemctl set-default graphical.target   # 回滚

# 3) 只对下一次启动生效，不改默认目标
#    在 GRUB 启动项临时加：systemd.unit=tdesktop-session.target
```

`tdesktop-install --set-default` 必须二次确认并打印回滚命令；默认安装**不**调用它。

### 7.5 备选：getty autologin 方案（PAM 集成有问题时）

如果自定义 `PAMName` 在目标发行版上不可靠，用现有 `login` 会话：

```ini
# /etc/systemd/system/getty@tty12.service.d/autologin.conf
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin YOURUSER --noclear %I $TERM
```

用户 shell 启动脚本（`~/.bash_profile` 或 `~/.profile`）中：

```sh
if [ "$(tty)" = "/dev/tty12" ] && [ -z "$TDESKTOP_SESSION" ]; then
  export TDESKTOP_SESSION=1
  exec /usr/bin/tdesktop --session
fi
```

优点：完全复用系统登录/PAM/logind；缺点：依赖用户 shell 配置，且 `exec` 后退出会回到 getty（由 `Restart` 决定）。安装脚本可选生成。

### 7.6 手动启动

- `tdesktop`：自动检测当前环境。
  - 真 TTY（`TERM=linux` 且 `isatty(0)`）→ 正常 TTY 模式。
  - GUI 终端 → 打印提示并拒绝，除非显式 `--gui-debug`（仅开发用）。
- **直接启动不启动 `tdesktop-manager`、不创建 PAM/logind 会话、不改写环境**：
  - uid/gid/groups、cwd、umask、rlimit 全部继承；
  - `PATH`、`HOME`、`SSH_AUTH_SOCK`、`XDG_*`、locale、`TERM` 原样保留；
  - 不 source `/etc/profile` / `~/.bash_profile`；
  - 只追加 `TDESKTOP_*` 和子 pane 的 `TERM=xterm-256color` 等覆盖项。
- `tdesktop --session`：供 target/manager 使用；退出时不再打印“按任意键返回 shell”，直接结束会话。没有 manager 时也可以手动用，但仍使用当前 shell 环境。
- 单实例锁：`/run/tdesktop/tty12.lock`（按 VT/用户区分），避免 systemd 与手动启动打架；第二个实例提示并退出。
- 手动启动不修改任何系统配置；退出后清屏/恢复 termios，回到原 shell。
- `tdesktop --diagnostics` 显示身份/环境来源/是否 managed/PAM 会话/TERM/VT，但不打印敏感变量值。

### 7.7 安装器与回滚

`tdesktop-install`（或 `make install` + 脚本）：

```sh
sudo tdesktop-install --user "$USER" --tty tty12          # 安装 units，不 enable
sudo tdesktop-install --user "$USER" --tty tty12 --enable # 开机共存启动
sudo tdesktop-install --user "$USER" --tty tty12 --set-default  # 独占（危险，需确认）
sudo tdesktop-install --uninstall                         # 移除 units/锁/覆盖，恢复 getty
```

安全检查：

1. `systemd-analyze verify tdesktop.target tdesktop.service`
2. 检查 tty12 未被 getty/display-manager/其他会话占用
3. 安装/卸载前备份将被覆盖的 `getty@tty12` drop-in
4. 不修改 `default.target`、`display-manager.service`、`graphical.target`（除非 `--set-default`）
5. 卸载后 `systemctl daemon-reload`，恢复 tty12 getty

### 7.8 “登录管理器”模式（1.2，范围外）

如果要的是类似 GDM 的**登录界面**而不是会话 target：

- 增加 `tdesktop-greeter.service`：在 tty1 上运行登录 UI，调用 PAM 认证；
- 认证成功后，greeter 以目标用户启动 `tdesktop.service`（切换 VT 或直接 exec）；
- 需要处理 PAM 会话、密码输入、失败计数、锁屏/登出、root 保护；
- 旧版本已有 PAM/shadow 代码可复用，但不属于 v1；先做 7.3 的会话 target。

---

## 8. 里程碑计划

| 阶段 | 内容 | 产出/验收 | 预估 |
|---|---|---|---|
| **M0 骨架** | CMake、FTXUI、TTY 检测/ConsoleAdapter、Home 占位、KeyRouter + ESC/高位序列、退出恢复 | 在 `TERM=linux` 真 TTY 上启动/退出、16 色、`Alt+Shift+X` 能退出；键位单测 | 1 天 |
| **M1 桌面** | 应用列表、搜索、选中、启动接口（先用 `/bin/sh -c 'echo hello; sleep 2'` 假会话） | Home 可操作；启动后进入全屏占位视图 | 1 天 |
| **M2 真终端** | `forkpty` + libvterm + `VTermNode` 绘制、reader 线程、快照、16 色降级 | `vim`/`htop`/`bash` 在 TTY 上画面正确，可输入 | 2 天 |
| **M3 控制** | 按键转发（含特殊键/UTF-8）、`TIOCSWINSZ`、退出流程、`SIGCHLD`/reap | 应用内 `Ctrl+C`、方向键、resize、退出均正常 | 1 天 |
| **M4 隐藏/多会话** | Home 运行中列表、`Alt+Shift+H` toggle、会话切换、Toast | 隐藏后 `vim` 仍活；恢复后画面与光标正确 | 1 天 |
| **M5 健壮性** | 崩溃恢复、进程组清理、输出洪水、宽字符/缺字形、配置键位、VT 切换保护 | 验收清单全绿 | 1.5 天 |
| **M6 系统集成** | `tdesktop.target`/`tdesktop-session.target`、`tdesktop.service`、PAM/logind 验证、安装器、tty12 检查、GUI 共存/回滚 | 手动启动、加法 target、独占 target 三种模式可切换；GUI 不受影响 | 1.5 天 |
| **M7 打磨** | 帮助页、确认框、文档、CI、打包、TTY 安装说明 | 1.0.0 tag | 1 天 |
| **M8（1.1 可选）** | evdev Broker 实现真实 `Win+q/h/x`；权限/热插拔/杂散字符过滤/活动 VT 判断 | 本地 TTY 上 Win 组合可用，失败自动回退 | 1 天 |

合计约 **11 人日**（含 1.1 的 Win 可选模块）。关键路径是 M2（libvterm 接入）；如果 libvterm 不可用，M2 可以先用 tmux 后端过渡，但 1.0 发布前仍建议换回 libvterm。

---

## 9. 验收清单

- [ ] 在真 TTY（`/dev/tty1`，`TERM=linux`，无 X/Wayland）上启动，全屏正常；退出后清屏、光标可见、termios 恢复，控制台可直接继续登录/使用。
- [ ] 不依赖 alternate screen：`?1049` 被忽略也不影响绘制与退出恢复。
- [ ] 颜色只使用 16 色；`vim`/`htop` 的 256 色/RGB 输出被正确降级，无乱码、无 truecolor 序列泄漏。
- [ ] `vim`：打开、编辑、`:wq`、`Ctrl+C`、方向键、`Esc` 均正常。
- [ ] `htop`：颜色、刷新、`F10` 退出正常；CPU 占用不高于直接运行 htop 的 1.2 倍。
- [ ] `bash`：`readline`、历史、Tab 补全、`Ctrl+R`、`Ctrl+L` 正常。
- [ ] `Alt+Shift+H` 隐藏后进程仍在（`ps` 可见），再次 `Alt+Shift+H` 恢复原画面与光标。
- [ ] `Alt+Shift+Q` 结束应用后回到 Home，`ps` 中无残留进程/僵尸。
- [ ] TTY 的 Alt 在 ESC 前缀模式和第 8 位模式下，快捷键都能触发（两种序列均注册）。
- [ ] 在无 F8~F12 的 60% 键盘上，所有默认键位可用，不依赖功能键。
- [ ] `Alt+F1..F12` 仍由内核切 VT，TDESKTOP 不拦截、不破坏。
- [ ] 控制台尺寸变化（`stty rows/cols` 或字体调整）后，应用立即重排，无花屏。
- [ ] 隐藏状态下应用持续输出（`ping`），恢复时显示最新画面，桌面不卡死。
- [ ] 桌面被 `SIGINT` / `SIGTERM` 杀死后 TTY 仍可用（光标/回显/颜色恢复）。
- [ ] 控制台字体缺字时自动降级 ASCII 边框/占位符，不出现大面积乱码。
- [ ] 默认键位不与常见 TUI 应用冲突：`Alt+Q/H`、`Ctrl+Q` 等仍能正常传给 vim/htop/bash。
- [ ] 可选：启用 evdev Broker 后，真实 `Win+q/h/x` 可用；无 `input` 权限时自动回退，不影响其他功能。
- [ ] 手动 `tdesktop` 在 tty12 启动/退出正常；不会修改 systemd 配置，不创建 PAM/logind 会话。
- [ ] 直接启动前后 `env` 对比：除 `TDESKTOP_*`/pane 终端变量外无变化；cwd、`SSH_AUTH_SOCK`、`PATH` 保留。
- [ ] 直接启动不要求 `tdesktop-manager` 存在；`ps` 中没有 manager 进程。
- [ ] target 启动由 `tdesktop-manager` 建立 logind 会话；manager env 通过 envp 传递；`loginctl` 可见会话，cwd=`$HOME`。
- [ ] `systemctl start tdesktop.target` 在 tty12 拉起会话，GDM/桌面仍正常运行，`default.target` 不变。
- [ ] `systemctl enable tdesktop.target` 后重启：GUI 与 TUI 共存，`Ctrl+Alt+F12`/`Ctrl+Alt+F7` 可来回切换。
- [ ] `systemctl stop tdesktop.target` 后 tty12 恢复 getty 登录提示，无黑屏/死 VT。
- [ ] `tdesktop-install --uninstall` 清理 units、锁文件、tty12 drop-in，GUI 启动链路不受影响。
- [ ] 独占模式必须显式 `--set-default`/`isolate`；`systemctl set-default graphical.target` 可回滚。
- [ ] evdev Win 热键只在 TDESKTOP 所属 VT 为活动 VT 时触发；切到 GUI 后不会误触。
- [ ] SSH / GUI 终端只作为调试环境，不作为 v1 验收阻塞项。

---

## 10. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| TTY 没有 Super 修饰位 | 无法从 `/dev/tty` 实现 `Win+q/h` | 默认 `Alt+Shift+H/Q/X`；1.1 可选 evdev Broker 或 keyd/kanata remap |
| Alt 编码因控制台配置不同 | 快捷键在 ESC 前缀/第 8 位模式下表现不一致 | KeyRouter 同时注册 `1b 48` 与 `0xC8` 等等价序列 |
| TTY 只有 16 色 | 应用 256 色/RGB 输出失真或出现转义垃圾 | 强制 `Palette16`；RGB→最近 16 色；禁用 truecolor |
| 无 alternate screen | 退出后屏幕/光标残留 | 不依赖 `?1049`；ConsoleAdapter 主动清屏 + 恢复 termios |
| 控制台字体缺字形 | 宽字符/emoji/边框花屏 | ASCII 边框/占位符降级；检测 UTF-8 locale |
| libvterm 渲染细节（宽字符、emoji、组合字符） | 画面错位 | `width==2` 跳过下一格；`chars[]` 多 codepoint 拼接；增加快照单测 |
| 输出洪水 | UI 卡顿 | 全速读 + 60fps 重绘 + 隐藏不绘制 |
| 子进程不响应 SIGTERM | 退出卡住 | 2s 后 SIGKILL；桌面退出最多等 5s |
| 崩溃后 TTY 残留 | 光标/回显/颜色异常 | 信号恢复序列 + `set_terminate` + `--screen=tty` 调试 |
| VT 切换误触 | 桌面被切走或快捷键失效 | 不绑定 `Alt+F1..F12`；文档说明 |
| evdev 杂散字符 | 按 Win+q 时应用多收到一个 q | InputBroker 按 Meta 状态过滤；`EVIOCGKEY` 兜底；失败回退 |
| 无 input 权限 | Win 热键不可用 | 自动检测并回退默认键位；文档给 udev/input 组方案 |
| systemd target 抢占 VT/getty | GUI 或登录提示被破坏 | 只用 tty12；仅冲突 `getty@tty12`；停止后恢复 getty；安装前检查占用 |
| PAMName/logind 集成失败 | 服务起不来或权限异常 | M6 做 PAM smoke test；失败退回 getty autologin 方案 |
| 无意改变默认启动目标 | 重启后 GUI 消失 | 安装器默认不碰 `default.target`；`--set-default` 二次确认 + 回滚提示 |
| 安装器覆盖系统 unit | 升级/卸载残留 | 使用 `/usr/local/lib/systemd/system`，备份/恢复 drop-in，`systemd-analyze verify` |
| evdev 在 GUI 活动时误触 | 用户在 GUI 按 Win+q 影响 TUI | 检查 `/sys/class/tty/tty0/active`，仅本 VT 活动时响应 |
| direct/managed 环境混淆 | 命令行启动被错误套上 PAM/服务环境 | `tdesktop` 永不启动 manager；direct 不 source profile、不改 env；加环境继承测试 |
| manager 常驻/泄漏 | 退出后残留进程、PAM 会话未关闭 | v1 manager bootstrap 后 `exec`，不常驻；systemd 跟踪主进程；测试失败路径 |
| autologin 安全 | 物理访问即登录 | 默认不启用 autologin；文档说明风险；需要登录管理器时走 PAM greeter（1.2） |

---

## 11. 后续（1.1 / 2.x）

- **1.1**：evdev Win 热键（可选）、GPM/evdev 鼠标、scrollback、bracketed paste、应用标题、会话切换。
- **1.2**：PAM 登录管理器（greeter）、多标签/分屏、命令面板、主题、配置热重载、`.desktop` Exec 完整解析。
- **2.x**：窗口化（浮动/平铺）、工作区、窗口动画、拖拽、桌面背景。

> 2.x 的窗口化可以直接复用 `TerminalSession + VTermNode`：`VTermNode` 放进带边框的 FTXUI 容器即可，这也是把终端层与视图层解耦的主要收益。

---

## 12. 需要确认的决策

1. **desktop manager 语义**：已确认指**桌面会话 target**；GDM 式登录管理器放 1.2。
2. **manager 使用范围**：`tdesktop-manager` 只被 target 使用；命令行 `tdesktop` 直接继承 shell 的 uid/gid/env/cwd，不创建 PAM 会话。是否确认这条硬边界？
3. **加法 target 默认值**：是否接受 `tdesktop.target` 使用 **tty12**、默认不 enable、不修改 `graphical.target`/`default.target`？需要开机共存时再 `enable`。
4. **target 环境来源**：优先 PAM `pam_env`/`environment.d` 合成；是否需要 `--login-env`（启动 `$SHELL -l -c env` 捕获登录环境）作为可选？
5. **PAM 集成方式**：优先 `PAMName=login` 的原生 service；如果目标发行版失败，退回 getty autologin + shell `exec`。
6. **独占模式**：是否需要 `tdesktop-session.target`（`Conflicts=graphical.target`）？默认只安装、不启用；`--set-default` 必须显式确认。
7. **快捷键**：默认 `Alt+Shift+H/Q/X`。是否把真实 `Win+q/h/x` 排进 1.1（evdev Broker，需要 `input` 组/udev 权限）？
8. **后端**：是否接受新增 `libvterm-dev` 依赖？
9. **退出桌面时**：运行中的应用是全部结束（建议），还是尽量保留为后台进程（需要 daemon 化，v1 不做）？
10. **安装权限**：安装器需要 root 写 systemd units/tty12 getty drop-in；哪些操作允许自动做，哪些必须打印命令让用户手动执行？

# TDESKTOP 实现规划：命令行体验 × 桌面体验

> 配套文档：[design-v1-fullscreen.md](design-v1-fullscreen.md)
> 目标环境：Linux TTY（`/dev/tty*`，`TERM=linux`），不依赖 X/Wayland。
> 本文回答两个问题：**产品上如何融合 CLI 与桌面体验**，**工程上按什么顺序实现**。

---

## 1. 定位

不是“终端里的窗口管理器”，也不是“又一个 tmux”。定位是：

> **一个以命令行为核心的 TTY 桌面环境。**
> 打开就能用 shell；需要多任务时像桌面一样切窗口、工作区；需要强大操作时像 tmux/zellij 一样用键盘和命令组合。

设计原则：

1. **Command-first**：任何可点击/可按键的操作都必须先是一个命令（`Command`），键盘、命令面板、CLI、脚本共用同一套命令。
2. **Keyboard-first**：v1 不依赖鼠标；所有功能可纯键盘完成。
3. **CLI-native**：shell 不是“附件”，而是第一等公民；分屏、工作区、通知都应能由 shell 命令驱动。
4. **Stateful but recoverable**：布局、工作区、会话可保存；崩溃/切换 VT 不破坏终端。
5. **TTY-native**：16 色、无鼠标、无 alternate screen、无剪贴板服务；不假装自己是 GUI。
6. **Progressive disclosure**：新手只看到 shell + 一个快捷键；高级用户通过命令面板/配置解锁分屏、工作区、脚本化。
7. **不阻塞输入**：终端输出洪水、应用卡死、崩溃恢复都不能让桌面失去响应。

---

## 2. 体验模型：CLI 与桌面的对应关系

### 2.1 层级

```
Session（一次 TDESKTOP 运行）
 └─ Workspace 1..9          ← 桌面隐喻（工作区）
     └─ Window（Tab）        ← 桌面窗口 / tmux window
         └─ Pane（PTY）      ← 真正的终端会话
             └─ Process      ← bash / vim / htop / ssh
```

v1.0 只有一层 Pane；但**模型从第一天就按树实现**，v1.1 加 Tab/Split 时不重写核心。

| 桌面概念 | CLI/tmux 对应 | TDESKTOP 实现 |
|---|---|---|
| 桌面 | session | `Session` |
| 工作区 | session group | `Workspace` |
| 窗口 | tmux window / tab | `Window`（含 pane 布局树） |
| 分屏 | tmux pane | `Pane`（PTY + libvterm） |
| 任务栏 | status line | 底部/顶部状态栏 |
| 开始菜单 | launcher | Home + fuzzy launcher |
| 运行对话框 | command palette | `Alt+Shift+P` |
| 通知 | shell bell / notify-send | Toast + 通知中心 |
| 剪贴板 | tmux copy mode | 内部 kill-ring + copy mode |
| Alt+Tab | tmux last-window | 会话切换器 |
| 窗口管理器 | tmux layout | `LayoutNode`（split tree） |

### 2.2 输入模式（Mode Stack）

| 模式 | 进入方式 | 行为 |
|---|---|---|
| `Desktop` | Home / 无前台应用 | 全局命令、启动器、工作区 |
| `App` | 打开应用 | 按键转发给 PTY；只拦截显式全局键 |
| `Palette` | `Alt+Shift+P` | 模糊搜索所有命令/应用/会话 |
| `Copy` | `Alt+Shift+C` | vi 风格选择/复制，不转发给应用 |
| `Resize` | `Alt+Shift+R` | 方向键调整分屏，Enter/Esc 退出 |
| `Confirm` | 危险操作 | y/n 确认 |
| `Help` | `Alt+?` | 键位表/命令列表 |

规则：

- `Palette` / `Confirm` / `Help` 是覆盖层，退出后回到之前的模式。
- `App` 模式下，`Ctrl+C`、`Ctrl+Z`、`Esc`、方向键等**必须原样转发**；只有键位表中显式注册的全局组合才拦截。
- 每个模式有独立 keymap；配置文件按 `[mode.app]` 覆盖。

### 2.3 命令系统（核心）

所有能力先注册为命令：

```cpp
struct Command {
  std::string id;                 // "session.new-tab"
  std::string title;              // "新建标签页"
  std::string category;           // "Session" / "Workspace" / "App"
  std::vector<KeyChord> keys;     // 默认键位
  std::function<bool(const Context&)> enabled;
  std::function<void(Context&)> run;
};
```

同一命令的四个入口：

```
键盘 ─┐
Palette ─┼─▶ CommandRegistry ──▶ ActionContext ──▶ Session/UI
CLI/IPC ─┤
Shell ───┘（tdesktop action ...）
```

首批命令（v1.0）：

| id | 默认键 | 说明 |
|---|---|---|
| `app.open` | Home Enter | 启动应用 |
| `app.hide` | `Alt+Shift+H` | 隐藏/恢复当前应用 |
| `app.exit` | `Alt+Shift+Q` | 结束当前应用 |
| `desktop.quit` | `Alt+Shift+X` | 退出桌面 |
| `palette.open` | `Alt+Shift+P` | 命令面板 |
| `help.show` | `Alt+?` | 帮助 |
| `session.new-tab` | v1.1 | 新建标签页 |
| `session.split` | v1.1 | 分屏 |
| `pane.close` | v1.1 | 关闭当前 pane |
| `copy.enter` | v1.1 | 复制模式 |
| `workspace.goto` | v1.2 | 切换工作区 |
| `notify.center` | v1.2 | 通知中心 |

好处：

- 命令面板不需要维护第二套逻辑；
- 用户可以配置任意键位；
- CLI/脚本与 UI 行为一致；
- 测试可以直接调用命令，不依赖屏幕。

### 2.4 Command Palette（体验核心）

```
┌ 命令面板 ──────────────────────────────────────┐
│ > vim                                       │
├────────────────────────────────────────────────┤
│ 应用                                            │
│   vim                    在 ~/src 打开          │
│   nvim                   在 ~/src 打开          │
│ 命令                                            │
│   session.new-tab        新建标签页      Alt+Shift+T │
│   session.split          垂直分屏        Alt+Shift+S │
│ 文件                                            │
│   ~/src/main.cpp         用 vim 打开            │
└────────────────────────────────────────────────┘
```

输入前缀：

| 前缀 | 含义 |
|---|---|
| 无 | 模糊搜索应用 + 命令 + 会话 |
| `>` | 只搜命令 |
| `:` | 执行 TDESKTOP 命令（如 `:split`） |
| `!` | 执行 shell 命令（新 pane，显示输出） |
| `/` | 在当前 pane 的 scrollback 中搜索 |
| `@` | 搜索工作区/会话 |

交互：

- `Enter` 打开；`Ctrl+Enter` 在 split 中打开；`Alt+Enter` 在新 tab 打开；
- `Tab` 补全；`Ctrl+N/P` 上下；`Esc` 关闭；
- 历史命令、最近应用、cwd 相关文件优先排序。

### 2.5 Home（桌面主界面）

```
┌ TDESKTOP ─────────────────────────────── 14:32 ─┐
│ 工作区 1  2  3                                   │
│                                                  │
│  运行中                                          │
│   ● bash        ~/src                            │
│   ● vim         ~/src/main.cpp                   │
│                                                  │
│  快速启动                                        │
│   > shell   htop   vim   python3                 │
│                                                  │
├──────────────────────────────────────────────────┤
│ Alt+Shift+P 命令   Alt+Shift+H 恢复   Alt+? 帮助  │
└──────────────────────────────────────────────────┘
```

- 默认启动进入 Home（可配置 `startup = shell` 直接进 shell）。
- Home 上直接打字 = 搜索；`Enter` 打开选中项。
- 运行中会话显示 cwd/命令名（来自 shell integration，v1.1）。

### 2.6 现代 CLI 体验（v1.1+）

这些功能让 TDESKTOP 不只是“终端里开了几个 shell”：

1. **Scrollback + Copy Mode**
   - libvterm 的 `sb_pushline` 回调维护每 pane 环形缓冲；
   - `Alt+Shift+C` 进入 vi 风格复制模式；`y` 复制到内部 kill-ring；
   - 内部剪贴板可被命令面板/其他 pane 粘贴；TTY 下不依赖 X clipboard。
2. **Shell Integration（OSC 133 / OSC 7）**
   - 通过 `OscSniffer` 旁路解析 PTY 字节流，提取 cwd、命令开始/结束、exit code；
   - 新 tab/split 继承 cwd；
   - 长命令完成时 Toast（`make` 完成）；
   - 窗口标题显示当前命令；
   - 失败命令标记（exit != 0）。
3. **Command Blocks（类似 Warp，但轻量）**
   - 每个命令的输出区间可导航/复制/重跑；
   - v1.2 先做“命令完成通知 + 复制输出”，不做完整块编辑器。
4. **Session Switcher**
   - `Alt+Shift+P` → `@` 搜索会话；或专用 `session.switch`；
   - 显示 workspace/tab/pane 树，Enter 跳转。
5. **Layout 保存/恢复**
   - 工作区、tab、split 比例、每个 pane 的 cwd 保存到 `~/.local/state/tdesktop/session.json`；
   - 启动时可 `--restore` 恢复布局并重启 shell（不保证进程状态，v2 由 server 解决）。
6. **脚本化**
   - `tdesktop action session.split --vertical --cwd "$PWD"`（v1.2，经 Unix socket 控制运行中的实例）；
   - `tdesktop notify "build done"` 从 shell 调用；
   - 命令面板里每个命令都显示 id，方便写脚本。

### 2.7 桌面体验（v1.2+）

- **工作区**：1~9，状态栏显示；切换键可配置，默认走命令面板（避免和 app 冲突）。
- **状态栏**：可选 1 行：工作区、pane 标题、时钟、负载、通知数；TTY 16 色。
- **通知**：Toast + 通知中心；来源：命令完成、bell、脚本 `tdesktop notify`。
- **窗口/分屏**：v1.1 先在“全屏 window”内做 tab/split；v1.2 加窗口边框/焦点色；v2 做浮动/平铺窗口管理器。
- **鼠标**：v1.1 GPM/evdev 可选；不作为核心。
- **主题**：16 色主题（default/dark/light/amber），ASCII/Unicode 边框切换。
- **登录管理器**：1.2 可选 PAM greeter；不属于 v1 核心。

---

## 3. 工程架构

### 3.1 分层

```
┌──────────────────────────────────────────────┐
│ cli/            tdesktop / tdesktop-install  │
├──────────────────────────────────────────────┤
│ ui/             FTXUI: Home/Palette/Status/  │
│                 CopyMode/Toast/Theme         │
├──────────────────────────────────────────────┤
│ core/           Session/Workspace/Window/    │
│                 Pane/Layout/CommandRegistry  │
├──────────────────────────────────────────────┤
│ terminal/       Pty/VtScreen/Scrollback/     │
│                 OscSniffer/ShellIntegration  │
├──────────────────────────────────────────────┤
│ platform/       ConsoleAdapter/Keymap/       │
│                 Evdev/Systemd/Logging        │
└──────────────────────────────────────────────┘
```

硬规则：

- `core/` **不 include FTXUI**；可以在无屏幕环境下单测。
- `ui/` 只读模型快照，不直接 `read()` PTY。
- `platform/` 封装所有 Linux/TTY 细节；`termios`/`ioctl` 不散落在 UI。
- 终端引擎（libvterm）与视图（FTXUI）解耦；2.x 窗口化直接复用。

### 3.2 数据流

```
PTY reader thread ── bytes ──▶ VtScreen + OscSniffer
                                   │
                                   ├─ dirty → PostEvent(Custom)
                                   └─ metadata (cwd/status/title)
UI thread ── snapshot ──▶ VTermNode ──▶ FTXUI Screen
UI thread ── key ──▶ KeyRouter ──▶ CommandRegistry ──▶ Action
```

- 模型只在 UI 线程修改；reader 线程只写自己的 `Pane` 快照/元数据（锁粒度到 pane）。
- 命令执行可能启动/停止进程，但等待/回收放到后台，UI 不阻塞。
- 所有跨线程通知合并到 60Hz。

### 3.3 关键模块

| 模块 | 内容 | 版本 |
|---|---|---|
| `core::SessionModel` | workspace/window/pane 树、焦点、z-order | v1.0 起 |
| `core::LayoutNode` | split tree、比例、最小尺寸 | v1.1 |
| `core::CommandRegistry` | 命令注册、keymap、enable 条件 | v1.0 |
| `core::ModeStack` | Desktop/App/Palette/Copy/Resize/Confirm | v1.0 起逐步 |
| `terminal::Pty` | forkpty、resize、SIGTERM→KILL、reap | v1.0 |
| `terminal::VtScreen` | libvterm 封装、快照 | v1.0 |
| `terminal::Scrollback` | `sb_pushline` 环形缓冲、copy mode | v1.1 |
| `terminal::OscSniffer` | OSC 7/133/9 旁路解析、cwd/exit code | v1.1 |
| `ui::DesktopView` | 组合 Home/App/Palette/Status/Toast | v1.0 |
| `ui::Palette` | fuzzy、分类、前缀语法 | v1.0 基础 / v1.1 完整 |
| `ui::CopyMode` | vi 选择、kill-ring | v1.1 |
| `platform::ConsoleAdapter` | TTY 检测、16 色、恢复 | v1.0 |
| `platform::InputBroker` | evdev Win、活动 VT 判断 | v1.1 可选 |
| `platform::ControlSocket` | `tdesktop action/notify` | v1.2 |
| `server::SessionServer` | detach/attach、多客户端 | v2.0 |

### 3.4 单进程 → 客户端/服务端

- **v1.x 单进程**：`tdesktop` 同时是 UI 和 PTY 拥有者。简单、崩溃恢复只到“布局”级别。
- **v2.0 拆分**：
  - `tdesktop-server`：拥有 SessionModel 和所有 PTY，后台常驻（systemd user service）；
  - `tdesktop`：UI 客户端，通过 Unix socket 连接；
  - 支持 detach/attach、崩溃后重连、多个前端（TTY + GUI 终端调试）；
  - v1 就把 `SessionService` 抽象出来，v2 只替换实现。

不建议 v1 就上 server：会显著增加 IPC、协议版本、权限、测试成本；先用接口隔离，v2 再拆。

### 3.5 两种启动上下文：direct / managed

这是本项目的硬边界：**管理器只在 target 启动时使用；命令行启动完全走 shell 身份与环境。**

| 维度 | 直接启动 `tdesktop` | target 启动（`tdesktop-manager`） |
|---|---|---|
| 身份 | 当前 shell 的 uid/gid/groups | systemd `User=` + PAM/logind 会话 |
| 环境变量 | **完整继承**调用者 `environ` | PAM `pam_env`、`/etc/environment`、`environment.d`、manager 默认值 |
| 当前目录 | 调用时的 cwd | `$HOME`（manager 设置） |
| 登录会话 | **不创建** | 创建（`PAMName=login`/logind） |
| 管理器 | **不启动 manager** | **唯一使用 manager 的场景** |
| state 目录 | `$XDG_STATE_HOME` 或 `~/.local/state` | 同上（由 manager 设置） |
| `TERM` | 继承（应为 `linux`） | manager 显式设为 `linux` |
| 单实例 | `flock`，按 VT/用户 | 同左 |
| 典型用途 | 开发、救援、临时使用、用户自己的 shell 工作流 | 开机 target、GUI 共存、账户会话 |

#### 直接启动规则（必须严格遵守）

1. **不读取、不重建、不清理**调用者的环境；`PATH`、`HOME`、`SSH_AUTH_SOCK`、`XDG_RUNTIME_DIR`、`DBUS_SESSION_BUS_ADDRESS`、locale、umask、rlimit 全部原样保留。
2. **不执行** `/etc/profile`、`~/.bash_profile`、`environment.d`；这些已经由用户的 shell 处理过。
3. **不创建** PAM/logind 会话，不改变 uid/gid，不 `chdir`。
4. 只追加 TDESKTOP 自己的变量（`TDESKTOP_*`）和为 pane 覆盖的终端变量。
5. 子 pane 的关键覆盖项：

```text
TERM=xterm-256color          # 内层由 libvterm 模拟
COLORTERM=truecolor          # 内层能力；外层再降级 16 色
TERM_PROGRAM=tdesktop
TDESKTOP_SESSION_ID=...
TDESKTOP_PANE_ID=...
```

  其余变量继承；**不要**为了“干净”而 unset `TMUX`、`SSH_AUTH_SOCK`、`DISPLAY`（GUI 调试时可能有用）。

6. 直接启动不要求 `tdesktop-manager` 二进制存在；安装包里 manager 可选。

#### target 启动规则

1. systemd 以 `User=YOURUSER` 启动 `tdesktop-manager --session`，通过 `PAMName=login` 建立 logind 会话。
2. manager 合成环境：
   - PAM `pam_env` / `/etc/environment`；
   - `~/.config/environment.d/*.conf`（如果使用 systemd user manager，可 `import-environment`）；
   - 安全默认值：`PATH`、`HOME`、`USER`、`LOGNAME`、`SHELL`、`XDG_*`；
   - `TERM=linux`、`TDESKTOP_MANAGED=1`、`TDESKTOP_VT=tty12`。
   > M6 必须先验证：systemd 的 `PAMName=login` 是否把 `pam_env` 的环境传给 service。若不会，target 改用 **getty autologin + shell `exec tdesktop-manager`**，或 manager 用 `$SHELL -l -c 'env -0'` 显式捕获登录环境。不要假设 PAM 环境一定可用。
3. manager `chdir($HOME)`，然后 `execve("/usr/bin/tdesktop", {"--session"}, envp)`：
   - 环境通过 **envp 继承**，不要放在命令行参数里（避免 `ps` 泄漏 token）；
   - `exec` 后 systemd 跟踪的主进程就是 UI，退出即服务结束；
   - 需要清理时可改为 `fork + wait + 传播退出码`，但 v1 不需要常驻 supervisor。
4. **manager 不读取用户的 shell rc**。target 模式的环境定义为“会话环境”，不是“某个交互 shell 的环境”；需要完全复刻 shell 环境时，提供 `--login-env` 显式选项（启动 `$SHELL -l -c 'env -0'` 捕获，需评估副作用）。
5. manager 只被 `tdesktop.service`/target 使用；**命令行 `tdesktop` 永不自动拉起 manager**。

#### 启动上下文接口

```cpp
struct LaunchContext {
  enum class Mode { Direct, Managed } mode;
  uid_t uid;
  gid_t gid;
  std::vector<gid_t> groups;
  std::vector<std::pair<std::string, std::string>> env;
  std::filesystem::path cwd;
  std::optional<std::string> session_id;  // managed 才有
  std::string tty;                        // /dev/tty12
};

LaunchContext BuildDirect();              // getuid/getgid/environ/getcwd
LaunchContext BuildManaged(/* from manager/env */);
```

测试要求：

- `tdesktop --diagnostics` 打印“身份/环境来源/是否 managed/PAM 会话/TERM/VT”，不打印敏感值；
- 直接启动前后对比 `env -0`：除 `TDESKTOP_*` 和 pane 变量外无差异；
- target 启动：`loginctl` 能看到会话，`TERM=linux`，cwd=`$HOME`，manager 不常驻。

---

## 4. 版本路线

### v1.0 — TTY Desktop Shell（约 2~2.5 周）

目标：**能日常替代“登录 shell + tmux 基本使用”**。

- 单 pane 全屏；Home + 应用启动器；
- **两种启动上下文**：`tdesktop` 直接启动继承 shell 身份/环境；`tdesktop-manager` 仅由 target 使用，负责 PAM/logind 会话与环境合成；
- `Alt+Shift+H/Q/X`；命令面板基础版（命令 + 应用搜索）；
- TTY 16 色、无鼠标、无 alt-screen 依赖；
- systemd：手动 + `tdesktop.target` 加法 + 可选独占 target；
- 配置（TOML）：键位、启动应用、主题、VT；
- 崩溃恢复、termios/屏幕恢复、进程组清理；
- 单测 + PTY 集成测试 + 真 TTY 手测 + 环境继承测试。

验收：打开 `vim`/`htop`/`bash`，隐藏/恢复/退出；GUI 共存 target 可开关；TTY 退出后可直接登录；`tdesktop` 直接启动不创建 PAM 会话、不改写 shell 环境。

### v1.1 — CLI Workspace（约 2~3 周）

目标：**达到 tmux/zellij 的日常多任务体验**。

- Tab + 水平/垂直 split；LayoutNode；
- 焦点切换、pane 关闭/缩放、Resize 模式；
- Scrollback + Copy Mode + 内部 kill-ring；
- 命令面板完整版：模糊、分类、`>`/`:`/`!`/`@` 前缀；
- Shell integration（OSC 133/7）：cwd 继承、命令完成通知、标题；
- Session switcher；布局保存；
- GPM/evdev 鼠标（可选）、evdev Win（可选）；
- 16 色主题 + ASCII 边框。

验收：4 个 pane 同时跑 `htop`/`vim`/`tail -f`/`bash`；复制粘贴；从 vim 退出后布局保持；Win 热键只在活动 VT 生效。

### v1.2 — Desktop Shell（约 3 周）

目标：**把“工作区 + 状态栏 + 通知 + 脚本化”补齐**。

- 工作区 1~9；状态栏（工作区、标题、时钟、通知）；
- 通知中心 + `tdesktop notify` + 命令完成 Toast；
- 窗口边框/焦点色；tab 栏；
- Command Blocks 轻量版：复制命令输出、重跑、失败标记；
- Unix control socket：`tdesktop action`/`notify`/`ls`；
- 主题、配置热重载、布局恢复；
- 可选 PAM greeter（登录管理器）——独立里程碑。

验收：从 shell 脚本控制桌面（`tdesktop action session.split`）；多工作区切换；命令完成通知；配置热重载。

### v2.0 — Window Manager + Session Server（约 4~6 周）

目标：**真正的“类桌面”**。

- 浮动/平铺窗口（先 tiling）；窗口移动/缩放/最大化；
- 多客户端 attach/detach；崩溃后会话存活；
- 插件/命令扩展（外部命令 + JSON 协议）；
- SSH/远程前端（可选，需要认证/转发设计）；
- 性能与内存优化。

> 不建议在 v1 做浮动窗口：TTY 屏幕小、无鼠标，平铺/tab 的收益远高于浮动。

---

## 5. 工程实施方式

### 5.1 仓库结构

```
CMakeLists.txt
src/
  main.cpp
  cli/            args, subcommands, install helper
  core/           model, commands, layout, modes
  terminal/       pty, vtscreen, scrollback, osc
  ui/             desktop, home, palette, status, copy, toast, theme
  platform/       console, keymap, evdev, signal, lock
  config/         toml load/validate/default
  ipc/            control socket (v1.2), server (v2)
tests/
  unit/           model, keymap, config, osc, color
  integration/    pty spawn/feed/snapshot
  snapshot/       FTXUI Screen::ToString() golden
docs/             user guide, keybindings, systemd, shell integration
packaging/        deb, systemd units, udev, shell hooks
```

### 5.2 依赖

| 依赖 | 用途 | 备注 |
|---|---|---|
| FTXUI 5.x | UI/渲染 | 必需 |
| libvterm | VT 解析 | 必需 |
| toml++（header-only） | 配置 | 少量依赖，优于手写 JSON |
| libevdev 或 raw `input_event` | evdev 热键/鼠标 | v1.1 可选 |
| CMake ≥ 3.25 | 构建 | |
| systemd | target/service | 可选运行模式 |

不引入：GUI 工具包、D-Bus 图形服务、X11/Wayland 库、tmux 运行时依赖。

### 5.3 测试策略

1. **单元测试**（无 TTY）：
   - Session/Layout 树、focus、序列化；
   - CommandRegistry、keymap、模式栈；
   - TOML 配置、校验、默认值；
   - OSC 133/7 解析（含跨包/分包）；
   - 颜色降级（RGB→16）、宽字符快照。
2. **PTY 集成测试**：
   - `forkpty` 启动 `sh -c 'printf ...'`，喂 ANSI，断言网格；
   - resize、退出/reap、SIGTERM→SIGKILL；
   - 输出洪水不阻塞。
3. **UI 快照测试**：
   - `Screen::ToString()` 固定 80×24，渲染 Home/Palette/Status；
   - golden 文件 + 差异输出；不依赖真终端。
4. **TTY/E2E**：
   - `systemd-analyze verify` 所有 units；
   - `systemd-nspawn`/QEMU 里跑 tty12 冒烟（CI nightly）；
   - 真机手测清单（design 文档第 9 节）。
5. **静态/动态**：
   - GCC/Clang、`-Wall -Wextra -Werror`；
   - ASan/UBSan 测试任务；
   - `shellcheck` 安装脚本/集成脚本。

### 5.4 性能预算

| 指标 | 目标 |
|---|---|
| 按键到屏幕 | < 30ms（本地 TTY） |
| 渲染 | 60fps 上限，空闲 0 重绘 |
| 启动到 Home | < 300ms |
| 内存 | 基础 < 30MB；每 pane < 5MB + scrollback 上限 |
| VT 吞吐 | `yes` 不卡 UI；丢帧不丢字节 |
| 隐藏状态 | 不重绘；仍消费输出 |
| 退出 | 2s SIGTERM 超时，5s 兜底 |

### 5.5 CI 与发布

- GitHub Actions：build matrix（Debian/Ubuntu/Fedora）、unit、PTY、snapshot；
- 夜间：systemd-nspawn TTY 冒烟、ASan；
- 发布物：源码 tarball、`.deb`、systemd units、shell integration、示例配置；
- 版本：`1.0.0` 起；配置/布局 JSON 带 `version` 字段，保证升级兼容；
- 文档：`user-guide.md`、`keybindings.md`、`systemd.md`、`shell-integration.md`、`troubleshooting.md`。

### 5.6 可观测性

- 日志：`~/.local/state/tdesktop/tdesktop.log`，默认 info，`--debug` 详细；
- 不把日志写进 TTY（会花屏）；
- 崩溃：`--crash-report` 保存最近 200 行日志 + 版本/TERM/VT；
- 命令面板 `> diagnostics` 显示：TTY、颜色能力、VT、活动 VT、输入权限、systemd 状态。

---

## 6. 体验细节清单

### 6.1 启动

- 默认 Home（显示运行中 + 快速启动）；配置 `startup = shell` 直接进 shell。
- systemd `--session` 不显示“按任意键”，退出即结束会话。
- 手动启动检测非 TTY：提示并拒绝，`--gui-debug` 例外。

### 6.2 新建/切换

- 启动应用时选择：当前 pane / 新 tab / 新 split / 新窗口（v2）。
- 命令面板 `Ctrl+Enter` = split，`Alt+Enter` = new tab。
- 切换：命令面板 + 可选直接键位；不强制用户记一堆快捷键。

### 6.3 关闭/退出

- 关闭 pane：若有前台进程，确认（可配置总是确认）。
- 退出桌面：确认所有运行中会话；默认全部结束。
- 隐藏：不询问，不杀进程。
- `Alt+Shift+Q` 只影响当前 pane/window，不误杀整个桌面。

### 6.4 通知

- 命令完成：仅当耗时 > 5s 或 exit != 0；避免噪音。
- Toast 1.5s，不抢焦点；通知中心可回看。
- `tdesktop notify` 从 shell 调用（v1.2），用于构建/长任务。

### 6.5 复制/粘贴

- v1.1 内部 kill-ring；`Alt+Shift+C` 复制模式；
- 若检测到 `wl-copy`/`xclip`，可选同步到系统剪贴板（GUI 终端调试用）；
- TTY 下不假装有系统剪贴板。

### 6.6 搜索

- 命令面板 `/` 搜索当前 pane scrollback；
- 应用/命令模糊搜索使用简单子序列匹配（无 fzf 依赖）；
- 文件搜索 v1.2 通过 shell（`fd`/`find`）或命令面板调用，不在 C++ 里实现文件索引。

---

## 7. 关键设计决策

| 决策 | 选择 | 理由 |
|---|---|---|
| 先窗口还是先分屏 | 先 tab/split，后窗口 | TTY 小屏、无鼠标；分屏/标签收益高 |
| 单体还是 server/client | v1 单体，v2 拆分 | 控制复杂度；接口先抽象 |
| 管理器使用范围 | **仅 target 启动** | 命令行启动必须保持 shell 身份/环境 |
| 直接启动环境 | 完整继承 shell `environ` | 不破坏用户工作流、SSH agent、XDG |
| target 环境 | PAM/environment.d 合成 | 可重复、可服务化，不依赖某个 shell |
| v1 manager 形态 | bootstrap + `exec`，不常驻 | 先解决会话/环境，v2 再做常驻 server |
| 配置格式 | TOML | 可读、可嵌套、header-only |
| 命令入口 | 统一 CommandRegistry | 键盘/面板/CLI/脚本一致 |
| 剪贴板 | 内部 kill-ring | TTY 无系统剪贴板 |
| 鼠标 | v1.1 可选 | 键盘优先 |
| 登录管理器 | 1.2 可选 | 先把会话 target 做稳 |
| 布局持久化 | JSON + version | 跨版本可迁移 |
| 主题 | 16 色 | TTY 限制 |
| 窗口管理器 | v2 平铺优先 | 浮动在 TTY 收益低 |

---

## 8. 风险与应对

| 风险 | 应对 |
|---|---|
| 功能膨胀，v1 做不完 | 严格按 v1.0 清单；tab/split 全部推到 v1.1 |
| 命令系统抽象过度 | 先实现 8~10 个真实命令，再抽象 |
| FTXUI 自定义 Node 性能 | 全量网格 60fps 先跑；必要时用 damage 局部重绘 |
| libvterm 与 FTXUI 线程冲突 | pane 级锁 + 快照；禁止跨线程直接读 vterm |
| shell integration 依赖用户配置 | 提供可选脚本，不影响基本功能 |
| systemd 集成因发行版差异失败 | PAM/getty 双方案 + verify + 回滚 |
| 会话持久化复杂 | v2 再做；v1 只保存布局 |
| direct/managed 环境不一致 | 文档明确“direct=shell 环境，managed=会话环境”；`--diagnostics` 显示来源；不要偷偷 source rc |
| manager 被误用于命令行 | `tdesktop` 永不自动拉起 manager；manager 只出现在 target unit 的 ExecStart |
| target 清理不彻底 | manager `exec` 不常驻；`ExecStopPost` 恢复 getty；崩溃/失败路径测试 |
| 快捷键冲突 | 统一 `Alt+Shift+字母` 家族；命令面板兜底；全部可配置 |
| 16 色主题难看 | 提供多套 16 色主题 + 用户自定义调色板 |

---

## 9. 第一版 backlog（可直接开工）

1. **B0 项目骨架**：CMake、FTXUI 依赖、`main`、日志、配置骨架、CI、`LaunchContext`（direct 继承 shell 环境）。
2. **B1 模型**：`SessionModel`、`Workspace`、`Window`、`Pane`、`LayoutNode`（v1 单 leaf）、序列化。
3. **B2 命令/键位**：`CommandRegistry`、`KeyRouter`（ESC/高位双编码）、`ModeStack`、8 个 v1 命令。
4. **B3 TTY**：`ConsoleAdapter`（TERM=linux、16 色、FixedSize、恢复）、`Pty`、`VtScreen`、`VTermNode`。
5. **B4 Home/Launcher**：应用扫描、fuzzy、启动。
6. **B5 App 视图**：全屏 pane、输入转发、resize、exit/hide。
7. **B6 命令面板基础版**：命令 + 应用搜索、`Ctrl+Enter` split 预留。
8. **B7 启动上下文/systemd**：`tdesktop-manager`（target-only，bootstrap+exec）、`tdesktop.service`/`tdesktop.target`/独占 target、安装器、tty12 检查、环境继承测试。
9. **B8 健壮性**：SIGTERM→KILL、reap、崩溃恢复、输出洪水、termios 恢复。
10. **B9 测试/文档**：PTY 集成、UI 快照、真 TTY 清单、用户文档、direct vs managed 环境说明。

建议顺序：B0 → B1 → B2 → B3 → B5（先跑通单 pane）→ B4 → B6 → B7 → B8 → B9。
B4/B6 可以在 B3 后面并行；B7 依赖 B5/B3 的稳定退出。

---

## 10. 不做什么（v1）

- 不做浮动窗口、窗口动画、拖拽；
- 不做内置文件管理器/编辑器/图片查看器；
- 不做插件 ABI、远程 SSH attach、多客户端；
- 不做系统剪贴板/通知守护进程；
- 不做常驻 manager/daemon（v1 的 `tdesktop-manager` 只是 target 用的 bootstrap；v2 才评估 detach/attach server）；
- 不做 PAM 登录管理器（1.2 可选）；
- 不追求 100% xterm/VT 兼容；以 libvterm 能力为准，缺什么再补。

---

## 11. 待确认

1. **manager 形态**：v1 的 `tdesktop-manager` 只做 target bootstrap（PAM/环境/exec），不常驻；v2 再评估常驻 server。可以吗？
2. **target 环境来源**：默认用 PAM/environment.d 合成；是否还需要 `--login-env`（启动 `$SHELL -l -c env` 捕获登录环境）？后者更接近 shell，但有副作用。
3. **direct 环境保留范围**：默认全量继承（含 `SSH_AUTH_SOCK`、`TMUX`、`DISPLAY`、`XDG_*`）；是否需要 `--clean-env` 作为显式选项？
4. **v1.0 是否就包含命令面板？** 建议包含基础版（命令+应用搜索），否则体验偏“普通终端”。
5. **分屏/标签放 v1.1 是否可接受？** v1.0 先单 pane，保证 TTY/systemd 稳定。
6. **配置格式用 TOML 还是继续 INI？** 建议 TOML。
7. **是否 v1.2 就做 Unix control socket？** 脚本化是“现代 CLI 体验”的关键，建议做。
8. **窗口化是否必须 v2？** 如果 v1.2 的 tab/split 已满足需求，浮动窗口可以继续推迟。
9. **PAM greeter 是否真的要？** 如果只是“登录后自动进桌面”，加法 target 足够，不需要 greeter。

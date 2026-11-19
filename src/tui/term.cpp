#include "tui/term.hpp"

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace apron::tui {

namespace {

termios g_saved{};
bool g_active = false;
std::atomic<bool> g_resized{false};

constexpr const char* kEnter = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J";
constexpr const char* kLeave = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";

void restore() {
  if (!g_active) return;
  g_active = false;
  ssize_t n = ::write(STDOUT_FILENO, kLeave, std::strlen(kLeave));
  (void)n;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
}

void on_signal(int sig) {
  restore();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

void on_winch(int) { g_resized = true; }

}  // namespace

Terminal::Terminal() {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return;
  termios raw = g_saved;
  raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return;
  g_active = true;
  std::atexit(restore);
  for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT}) std::signal(sig, on_signal);
  std::signal(SIGWINCH, on_winch);
  write(kEnter);
  query_size();
  ok_ = true;
}

Terminal::~Terminal() { restore(); }

void Terminal::suspend() {
  restore();
  write("\x1b[H\x1b[2J");  // a blank screen for the session to paint on
}

void Terminal::resume() {
  termios raw = g_saved;
  raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  g_active = true;
  std::signal(SIGWINCH, on_winch);  // the attached session installed its own
  write(kEnter);
  query_size();
}

void Terminal::query_size() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    w_ = ws.ws_col;
    h_ = ws.ws_row;
  }
}

void Terminal::write(const std::string& bytes) {
  size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = ::write(STDOUT_FILENO, bytes.data() + off, bytes.size() - off);
    if (n <= 0) return;
    off += static_cast<size_t>(n);
  }
}

KeyEvent Terminal::poll(int timeout_ms) {
  if (g_resized.exchange(false)) {
    query_size();
    return {Key::Resize, 0, ""};
  }
  pollfd pfd{STDIN_FILENO, POLLIN, 0};
  int r = ::poll(&pfd, 1, timeout_ms);
  if (g_resized.exchange(false)) {
    query_size();
    return {Key::Resize, 0, ""};
  }
  if (r <= 0) return {};
  char buf[16];
  ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
  if (n <= 0) return {};
  if (buf[0] == '\x1b') {
    if (n == 1) return {Key::Esc, 0, ""};
    if (n >= 3 && (buf[1] == '[' || buf[1] == 'O')) {
      switch (buf[2]) {
        case 'A': return {Key::Up, 0, ""};
        case 'B': return {Key::Down, 0, ""};
        case 'C': return {Key::Right, 0, ""};
        case 'D': return {Key::Left, 0, ""};
      }
    }
    return {};
  }
  if (buf[0] == '\r' || buf[0] == '\n') return {Key::Enter, 0, ""};
  if (buf[0] == '\t') return {Key::Tab, 0, ""};
  if (buf[0] == 0x7f || buf[0] == 0x08) return {Key::Backspace, 0, ""};
  if (buf[0] == 3) return {Key::Char, 'q', "q"};  // Ctrl-C quits
  return {Key::Char, buf[0], std::string(buf, static_cast<size_t>(n))};
}

}  // namespace apron::tui

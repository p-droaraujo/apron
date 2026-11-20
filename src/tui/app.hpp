// The screen around the board: a sidebar with projects, sessions and
// machines, a framed main pane with tabs, boxed cards with buttons, a footer
// with the keys that work here, and a hit map so the mouse can click them.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tui/progress.hpp"
#include "tui/screen.hpp"

namespace apron::tui {

// --- What the sidebar shows (filled by a poller in the CLI) -----------------

enum class SessionState { Working, Asking, Idle, Exited };

struct SessionRow {
  std::string machine;  // ssh host, or "" for this machine
  uint32_t id = 0;
  std::string what;     // the program in the foreground: the agent
  std::string cwd;
  SessionState state = SessionState::Idle;
  int64_t since_ms = 0;
};

struct ProjectGroup {
  std::string name;
  std::string machine;  // ssh host, or "" for this machine
  std::string root;     // the project's folder on that machine; "" when it is only a folder
  std::vector<SessionRow> sessions;
};

struct MachineRow {
  std::string name;
  bool here = false;
  bool ok = false;
  int running = 0;
  int64_t seen_ms = 0;  // last answer; 0 = never
  std::string error;
};

struct Fleet {
  std::vector<ProjectGroup> projects;
  std::vector<MachineRow> machines;
};

// The selectable rows of the sidebar, top to bottom.
struct SideRow {
  enum class Kind { Project, Session, Machine } kind = Kind::Project;
  int group = 0;  // index into projects, or into machines for Kind::Machine
  int item = 0;   // index into the group's sessions
};
std::vector<SideRow> side_rows(const Fleet& fleet);

// --- What the mouse can hit ---------------------------------------------------

enum class Act { None, Tab, Side, Card, Button, NewSession, AddMachine };

struct Hit {
  int x = 0, y = 0, w = 0, h = 0;
  Act act = Act::None;
  int a = 0, b = 0;
};
const Hit* hit_at(const std::vector<Hit>& hits, int x, int y);

// --- Buttons on the selected card -------------------------------------------

enum class Action { Terminal, Answer, Review, Log, Details };

struct Button {
  Action action;
  std::string key;    // "enter" for the first one, else its letter
  std::string label;
};

// --- Drawing ------------------------------------------------------------------

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

int sidebar_width(int screen_w);  // 0 when the screen is too narrow for one
void draw_box(Screen& sc, Rect r, Style border, bool heavy);

void render_sidebar(Screen& sc, Rect r, const Fleet& fleet, int selected, bool focused, int64_t now_ms,
                    std::vector<Hit>& hits);
// The main pane's frame with the title on the left and the tabs on the right;
// returns the inside.
Rect render_main_frame(Screen& sc, Rect r, const std::string& title, const std::vector<std::string>& tabs, int active,
                       bool focused, std::vector<Hit>& hits);
void render_board(Screen& sc, Rect r, const Model& model, const View& view, bool focused,
                  const std::vector<Button>& buttons, int64_t now_ms, std::vector<Hit>& hits);
void render_footer(Screen& sc, const std::vector<std::pair<std::string, std::string>>& keys, const std::string& status);
// A small box in the middle of `r`: a line of text being typed, or a question.
void render_prompt(Screen& sc, Rect r, const std::string& title, const std::string& text, bool typing);
void render_help(Screen& sc, Rect r);

}  // namespace apron::tui

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
  int cols = 80, rows = 24;  // the size the program draws for
};

struct ProjectGroup {
  std::string name;
  std::string machine;  // ssh host, or "" for this machine
  std::string root;     // the project's folder on that machine; "" when it is only a folder
  std::vector<SessionRow> sessions;
};

struct MachineRow {
  std::string name;
  std::string machine_name;  // what the machine calls itself (in runs: agent@machine)
  bool here = false;
  bool ok = false;
  int running = 0;
  int64_t seen_ms = 0;  // last answer; 0 = never
  std::string error;
};

struct Fleet {
  std::vector<ProjectGroup> projects;
  std::vector<MachineRow> machines;
  // The board of the project being watched on another machine, when there is one.
  bool has_board = false;
  std::string board_host, board_root;
  Model board;
};

// --- What can be pointed at -------------------------------------------------
//
// Everything a person can do is an element on the screen. Each frame the
// renderers list the elements they drew as hits; the arrows move the focus
// to the nearest one in that direction, Enter acts on it, and the mouse
// clicks it.

enum class Act {
  None,
  Tab,            // a: view (clicked, not focused: Tab cycles views)
  Project,        // a: project
  Session,        // a: project, b: session
  SessionEnd,     // a: project, b: session
  Machine,        // a: machine
  MachineRemove,  // a: machine
  AddMachine,
  Card,           // a: lane, b: index
  Button,         // a: Action, on the current card or overlay
  NewAgent,       // a placeholder that starts an agent
  Tile,           // a: tile
};

struct Target {
  Act act = Act::None;
  int a = 0, b = 0;
  bool operator==(const Target&) const = default;
};

struct Hit {
  int x = 0, y = 0, w = 0, h = 0;
  Target t;
  bool focusable = true;
};
const Hit* hit_at(const std::vector<Hit>& hits, int x, int y);
const Hit* find_hit(const std::vector<Hit>& hits, const Target& t);

enum class Dir { Up, Down, Left, Right };
// The focusable hit nearest to `from` in that direction, if there is one.
const Hit* next_hit(const std::vector<Hit>& hits, const Target& from, Dir dir);

// --- Buttons ------------------------------------------------------------------

enum class Action { Terminal, Answer, Review, Log, Details, Merge, Return, Discard };

struct Button {
  Action action;
  std::string label;
};

// --- Drawing ------------------------------------------------------------------

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

int sidebar_width(int screen_w);  // 0 when the screen is too narrow for one
void draw_box(Screen& sc, Rect r, Style border, bool heavy);
// A row of buttons, `[ Label ]`; the focused one in the accent color.
// Returns the width used.
int draw_buttons(Screen& sc, int x, int y, int w, const std::vector<Button>& buttons, const Target& focus,
                 std::vector<Hit>& hits);

void render_sidebar(Screen& sc, Rect r, const Fleet& fleet, const Target& focus, int64_t now_ms,
                    std::vector<Hit>& hits);
// The main pane's frame with the title on the left and the tabs on the right;
// returns the inside.
Rect render_main_frame(Screen& sc, Rect r, const std::string& title, const std::vector<std::string>& tabs, int active,
                       bool focused, std::vector<Hit>& hits);
// `view` names the current card: the one kept in sight, and the one whose
// buttons show while it or one of them has the focus.
void render_board(Screen& sc, Rect r, const Model& model, const View& view, const Target& focus,
                  const std::vector<Button>& buttons, int64_t now_ms, std::vector<Hit>& hits);
// The grid: one tile per live session, its terminal drawn from the bottom,
// and a last tile that starts a new agent.
struct Tile {
  std::string title;
  const class Vt* vt = nullptr;
};
void render_grid(Screen& sc, Rect r, const std::vector<Tile>& tiles, const Target& focus, std::vector<Hit>& hits);

void render_footer(Screen& sc, const std::vector<std::pair<std::string, std::string>>& keys, const std::string& status);
// A small box in the middle of `r`: a line of text being typed, or a question.
void render_prompt(Screen& sc, Rect r, const std::string& title, const std::string& text, bool typing);
void render_help(Screen& sc, Rect r);

}  // namespace apron::tui

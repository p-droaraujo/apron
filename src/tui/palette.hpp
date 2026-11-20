// The palette and small helpers shared by every screen: color only for state.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/runs.hpp"
#include "core/time.hpp"
#include "tui/screen.hpp"

namespace apron::tui {

// The v2 palette: color only for state.
inline const Style kText{Color::hex(0xe6e8eb), {}, false};
inline const Style kBold{Color::hex(0xe6e8eb), {}, true};
inline const Style kSoft{Color::hex(0xa3aab5), {}, false};
inline const Style kDim{Color::hex(0x7d8592), {}, false};
inline const Style kLine{Color::hex(0x3a404b), {}, false};
inline const Style kBlue{Color::hex(0x7aa2f7), {}, false};
inline const Style kBlue2{Color::hex(0x4a6bb0), {}, false};
inline const Style kAmber{Color::hex(0xe0af68), {}, false};
inline const Style kRed{Color::hex(0xf7768e), {}, false};
inline constexpr uint32_t kSelected = 0x1f2430;

inline Style with_bg(Style s, bool selected) {
  if (selected) s.bg = Color::hex(kSelected);
  return s;
}

inline Style state_style(RunState s) {
  switch (s) {
    case RunState::Working: return kBlue;
    case RunState::Waiting: return kAmber;
    case RunState::Failed: return kRed;
    default: return kDim;
  }
}

inline const char* state_glyph(RunState s) {
  switch (s) {
    case RunState::Working: return "●";
    case RunState::Waiting: return "◐";
    case RunState::Blocked: return "⊘";
    case RunState::Done: return "✓";
    case RunState::Failed: return "✕";
    case RunState::Abandoned: return "–";
  }
  return "●";
}

inline std::string ago(int64_t now, int64_t then) { return timeutil::short_duration(now - then); }

// Titles come from markdown; on screen, backticks help nobody.
inline std::string plain(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
  return s;
}


}  // namespace apron::tui

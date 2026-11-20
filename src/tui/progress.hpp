// Progress (Andamento): what tasks.md and the events hold, in four lanes.
#pragma once

#include "core/activity.hpp"
#include "core/board.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "core/project.hpp"
#include "core/review.hpp"
#include "core/runs.hpp"
#include "tui/screen.hpp"

namespace apron::tui {

using apron::Item;
using apron::Lane;
using apron::load_model;
using apron::Model;

struct View {
  int lane = 1;     // selected lane
  int index = 0;    // selected item in the lane
  bool help = false;
  int tick = 0;     // drives the breathing of working agents
};

void clamp_selection(const Model& model, View& view);


// A run's timeline, scrollable.
void render_log(Screen& screen, const Project& project, const std::string& run, int scroll, int64_t now_ms);
// The fields of a task with no run.
void render_task(Screen& screen, const Project& project, const std::string& task);

// Activity: the newest events of every project, newest first.
// Events of every project, newest first. Rows of `here` (the project in the
// pane's title) leave the project out.
void render_activity(Screen& sc, const std::vector<ActivityItem>& items, int scroll, int64_t now,
                     const std::string& here = "");

// One finished run with its instruments: trajectory, checks, scope, changes.
void render_review(Screen& screen, const Project& project, const RunSummary& run, const std::string& stat);

}  // namespace apron::tui

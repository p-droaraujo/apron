// Progress (Andamento): what tasks.md and the events hold, in four lanes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/project.hpp"
#include "core/review.hpp"
#include "core/runs.hpp"
#include "tui/screen.hpp"

namespace apron::tui {

enum class Lane { Plano, Executando, Portoes, Feito };

struct Item {
  Lane lane = Lane::Plano;
  std::string task;    // task id
  std::string title;
  std::string who;     // agent@machine
  std::string detail;  // last sentence, dependencies or reason
  std::string run;     // run id, if any
  RunState state = RunState::Working;
  bool ready = false;  // open task whose dependencies are done
  bool from_run = false;
  bool review = false;  // finished, waiting for a person
  int64_t since_ms = 0;  // run start or last event
};

struct Model {
  std::string project;
  std::string machine;
  std::vector<Item> lanes[4];
  int waiting = 0, working = 0, blocked = 0;
};

Model load_model(const Project& project);

struct View {
  int lane = 1;     // selected lane
  int index = 0;    // selected item in the lane
  bool help = false;
  int tick = 0;     // drives the breathing of working agents
};

void clamp_selection(const Model& model, View& view);

// The tab bar on the first line: Progress, Review (with its count), Activity.
void render_tabs(Screen& screen, int active, int review_count, bool activity);
void render_progress(Screen& screen, const Model& model, const View& view, int64_t now_ms);

// A run's timeline, scrollable.
void render_log(Screen& screen, const Project& project, const std::string& run, int scroll, int64_t now_ms);
// The fields of a task with no run.
void render_task(Screen& screen, const Project& project, const std::string& task);

// Activity: the newest events of every project, newest first.
struct ActivityItem {
  std::string project;
  Event event;
};
std::vector<ActivityItem> load_activity(const std::vector<std::string>& roots, size_t limit);
void render_activity(Screen& screen, const std::vector<ActivityItem>& items, int scroll, int64_t now_ms);

// Review: the queue of finished runs, and one run with its instruments.
void render_review_list(Screen& screen, const std::vector<RunSummary>& queue, int index, int64_t now_ms);
void render_review(Screen& screen, const Project& project, const RunSummary& run, const std::string& stat,
                   const std::string& status, const std::string* input);

}  // namespace apron::tui

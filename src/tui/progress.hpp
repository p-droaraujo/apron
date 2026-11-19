// Progress (Andamento): what tasks.md and the events hold, in four lanes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/project.hpp"
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
void render_progress(Screen& screen, const Model& model, const View& view, int64_t now_ms);

// A run's timeline, scrollable.
void render_log(Screen& screen, const Project& project, const std::string& run, int scroll, int64_t now_ms);
// The fields of a task with no run.
void render_task(Screen& screen, const Project& project, const std::string& task);

}  // namespace apron::tui

// The board of a project: tasks.md and the runs' events, sorted into four
// lanes. Built where the files are, and sent as JSON to a screen elsewhere.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "core/project.hpp"
#include "core/runs.hpp"

namespace apron {

enum class Lane { Plano, Executando, Portoes, Feito };

struct Item {
  Lane lane = Lane::Plano;
  std::string task;    // task id
  std::string title;
  std::string who;     // agent@machine
  std::string detail;  // last sentence, dependencies or reason
  std::string run;     // run id, if any
  std::string session; // the agent's terminal (aprond session id), if the run has one
  std::string machine; // where that session lives
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

json::Value board_to_json(const Model& model);
Model board_from_json(const json::Value& value);

}  // namespace apron

#include "core/runs.hpp"

#include <algorithm>

#include "core/i18n.hpp"
#include "core/time.hpp"

namespace apron {

std::string_view to_string(RunState state) {
  switch (state) {
    case RunState::Working: return "working";
    case RunState::Waiting: return "waiting for you";
    case RunState::Blocked: return "no power";
    case RunState::Done: return "done";
    case RunState::Failed: return "failed";
    case RunState::Abandoned: return "abandoned";
  }
  return "working";
}

std::string label(RunState state) { return tr(to_string(state)); }

RunSummary summarize(const std::string& id, const std::vector<Event>& events) {
  RunSummary s;
  s.id = id;
  bool first = true;
  for (const auto& e : events) {
    if (first || e.ts_ms < s.started_ms) s.started_ms = e.ts_ms;
    if (first) s.last_ms = e.ts_ms;
    first = false;
    s.last_ms = std::max(s.last_ms, e.ts_ms);
    if (s.task.empty()) s.task = e.task;
    if (s.agent.empty()) s.agent = e.agent;
    if (s.machine.empty()) s.machine = e.machine;
    // Notes are for the next run, not for the status of this one.
    if (!e.msg.empty() && e.kind != Kind::Note) s.last_msg = e.msg;
    switch (e.kind) {
      case Kind::RunStarted:
        s.intent = e.extra_str("intent");
        s.state = RunState::Working;
        break;
      case Kind::Step:
        ++s.steps;
        s.state = RunState::Working;  // moving again
        break;
      case Kind::Ask: s.state = RunState::Waiting; break;
      case Kind::Blocked: s.state = RunState::Blocked; break;
      case Kind::Note: break;
      case Kind::Check: {
        const json::Value* ok = e.extra_find("ok");
        if (ok && ok->is_bool() && ok->boolean()) ++s.checks_ok;
        else ++s.checks_failed;
        break;
      }
      case Kind::Review: s.review = e.extra_str("action"); break;
      case Kind::RunFinished: {
        std::string result = e.extra_str("result");
        s.state = result == "failed" ? RunState::Failed
                  : result == "abandoned" ? RunState::Abandoned
                                          : RunState::Done;
        if (const json::Value* f = e.extra_find("files"); f && f->is_array()) s.files = static_cast<int>(f->arr().size());
        if (const json::Value* o = e.extra_find("outside"); o && o->is_array())
          for (const auto& v : o->arr())
            if (v.is_string()) s.outside.push_back(v.str());
        break;
      }
    }
  }
  return s;
}

std::vector<RunSummary> list_runs(const Project& project) {
  std::vector<RunSummary> out;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(project.runs_dir(), ec)) {
    if (entry.path().extension() != ".jsonl") continue;
    auto events = load_events(entry.path());
    if (events.empty()) continue;
    out.push_back(summarize(entry.path().stem().string(), events));
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.started_ms > b.started_ms; });
  return out;
}

std::string new_run_id(const std::string& task, int64_t now_ms) {
  return "r-" + task + "-" + timeutil::compact_stamp(now_ms);
}

}  // namespace apron

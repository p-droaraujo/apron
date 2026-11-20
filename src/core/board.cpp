#include "core/board.hpp"

#include <algorithm>
#include <map>

#include "core/i18n.hpp"
#include "core/tasks.hpp"

namespace apron {

namespace {

// Titles come from markdown; on screen, backticks help nobody.
std::string plain(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
  return s;
}

const char* state_name(RunState s) { return to_string(s).data(); }

RunState state_from(const std::string& s) {
  for (RunState r : {RunState::Working, RunState::Waiting, RunState::Blocked, RunState::Done, RunState::Failed,
                     RunState::Abandoned})
    if (to_string(r) == s) return r;
  return RunState::Working;
}

}  // namespace

Model load_model(const Project& project) {
  Model m;
  m.project = project.root.filename().string();
  m.machine = machine_name();

  auto file = TaskFile::load(project.tasks_file());
  std::map<std::string, const Task*> by_id;
  if (file)
    for (const auto& t : file->tasks()) by_id[t.id] = &t;

  auto runs = list_runs(project);
  std::map<std::string, bool> task_has_run;
  for (const auto& r : runs) {
    Item it;
    it.from_run = true;
    it.task = r.task;
    it.run = r.id;
    it.state = r.state;
    it.who = r.agent + "@" + r.machine;
    it.session = r.session;
    it.machine = r.machine;
    auto found = by_id.find(r.task);
    it.title = plain(found != by_id.end() ? found->second->title : r.intent);
    it.detail = r.last_msg;
    switch (r.state) {
      case RunState::Working:
      case RunState::Blocked:
        it.lane = Lane::Executando;
        it.since_ms = r.started_ms;
        break;
      case RunState::Waiting:
        it.lane = Lane::Portoes;
        it.since_ms = r.last_ms;
        break;
      default:
        // A finished run waits in gates until a person reviews it.
        it.lane = r.needs_review() ? Lane::Portoes : Lane::Feito;
        it.since_ms = r.last_ms;
        it.review = r.needs_review();  // the lane says it waits; the card shows the summary
    }
    if (r.state == RunState::Working) ++m.working;
    if (r.state == RunState::Waiting || r.needs_review()) ++m.waiting;
    if (r.state == RunState::Blocked) ++m.blocked;
    // A task shows up once: through its most recent run.
    if (task_has_run[r.task] && it.lane == Lane::Feito) continue;
    task_has_run[r.task] = true;
    m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
  }

  if (file) {
    for (const auto& t : file->tasks()) {
      if (task_has_run[t.id]) continue;
      Item it;
      it.task = t.id;
      it.title = plain(t.title);
      it.who = t.field(kBy);
      if (t.state == ' ') {
        it.lane = Lane::Plano;
        auto why = blockers_to_start(*file, t);
        it.ready = why.empty();
        if (!it.ready) {
          std::string deps;
          for (const auto& d : t.depends()) deps += (deps.empty() ? "" : ", ") + d;
          it.detail = deps.empty() ? why.front() : "← " + deps;
        }
      } else if (t.state == '~') {
        it.lane = Lane::Executando;  // in progress on another machine
        it.detail = tr("on another machine");
      } else if (t.state == '?') {
        it.lane = Lane::Portoes;
        it.state = RunState::Waiting;
        it.detail = tr("waiting for a decision");
      } else if (t.state == 'x') {
        it.lane = Lane::Feito;
        it.state = RunState::Done;
      } else {
        continue;
      }
      m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
    }
  }
  // Plan: ready tasks first, in file order.
  std::stable_sort(m.lanes[0].begin(), m.lanes[0].end(), [](const Item& a, const Item& b) { return a.ready > b.ready; });
  return m;
}

json::Value board_to_json(const Model& m) {
  json::Object o;
  o.emplace_back("project", m.project);
  o.emplace_back("machine", m.machine);
  json::Array lanes;
  for (const auto& lane : m.lanes) {
    json::Array items;
    for (const auto& it : lane) {
      json::Object i;
      i.emplace_back("lane", static_cast<long long>(it.lane));
      i.emplace_back("task", it.task);
      i.emplace_back("title", it.title);
      i.emplace_back("who", it.who);
      i.emplace_back("detail", it.detail);
      i.emplace_back("run", it.run);
      i.emplace_back("session", it.session);
      i.emplace_back("machine", it.machine);
      i.emplace_back("state", std::string(state_name(it.state)));
      i.emplace_back("ready", it.ready);
      i.emplace_back("from_run", it.from_run);
      i.emplace_back("review", it.review);
      i.emplace_back("since", static_cast<long long>(it.since_ms));
      items.emplace_back(std::move(i));
    }
    lanes.emplace_back(std::move(items));
  }
  o.emplace_back("lanes", std::move(lanes));
  o.emplace_back("waiting", m.waiting);
  o.emplace_back("working", m.working);
  o.emplace_back("blocked", m.blocked);
  return json::Value(std::move(o));
}

Model board_from_json(const json::Value& v) {
  Model m;
  m.project = v.get_str("project");
  m.machine = v.get_str("machine");
  auto num = [](const json::Value& x, const char* k) {
    const json::Value* f = x.find(k);
    return f && f->is_number() ? f->num() : 0.0;
  };
  auto flag = [](const json::Value& x, const char* k) {
    const json::Value* f = x.find(k);
    return f && f->is_bool() && f->boolean();
  };
  if (const json::Value* lanes = v.find("lanes"); lanes && lanes->is_array()) {
    size_t li = 0;
    for (const auto& lane : lanes->arr()) {
      if (li >= 4 || !lane.is_array()) break;
      for (const auto& i : lane.arr()) {
        Item it;
        it.lane = static_cast<Lane>(li);
        it.task = i.get_str("task");
        it.title = i.get_str("title");
        it.who = i.get_str("who");
        it.detail = i.get_str("detail");
        it.run = i.get_str("run");
        it.session = i.get_str("session");
        it.machine = i.get_str("machine");
        it.state = state_from(i.get_str("state"));
        it.ready = flag(i, "ready");
        it.from_run = flag(i, "from_run");
        it.review = flag(i, "review");
        it.since_ms = static_cast<int64_t>(num(i, "since"));
        m.lanes[li].push_back(std::move(it));
      }
      ++li;
    }
  }
  m.waiting = static_cast<int>(num(v, "waiting"));
  m.working = static_cast<int>(num(v, "working"));
  m.blocked = static_cast<int>(num(v, "blocked"));
  return m;
}

}  // namespace apron

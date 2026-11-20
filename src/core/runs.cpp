#include "core/runs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>

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
        s.session = e.extra_str("session");
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

namespace {

// Run files only grow (one event per line, appended), so a file whose size and
// mtime did not change has the same summary: the board reloads every second and
// re-reads only the runs that moved.
struct CachedSummary {
  std::uintmax_t size = 0;
  std::filesystem::file_time_type mtime;
  RunSummary summary;
};
std::mutex g_cache_mutex;
std::map<std::filesystem::path, CachedSummary> g_cache;

}  // namespace

std::vector<RunSummary> list_runs(const Project& project) {
  std::vector<RunSummary> out;
  std::error_code ec;
  std::lock_guard lock(g_cache_mutex);
  std::set<std::filesystem::path> seen;
  for (const auto& entry : std::filesystem::directory_iterator(project.runs_dir(), ec)) {
    if (entry.path().extension() != ".jsonl") continue;
    std::error_code e1, e2;
    auto size = entry.file_size(e1);
    auto mtime = entry.last_write_time(e2);
    if (e1 || e2) continue;
    seen.insert(entry.path());
    auto it = g_cache.find(entry.path());
    if (it == g_cache.end() || it->second.size != size || it->second.mtime != mtime) {
      auto events = load_events(entry.path());
      if (events.empty()) continue;
      it = g_cache.insert_or_assign(entry.path(), CachedSummary{size, mtime, summarize(entry.path().stem().string(), events)}).first;
    }
    out.push_back(it->second.summary);
  }
  // Forget runs of this project that are gone.
  std::erase_if(g_cache, [&](const auto& kv) { return kv.first.parent_path() == project.runs_dir() && !seen.count(kv.first); });
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.started_ms > b.started_ms; });
  return out;
}

std::string new_run_id(const std::string& task, int64_t now_ms) {
  return "r-" + task + "-" + timeutil::compact_stamp(now_ms);
}

bool valid_run_id(std::string_view id) {
  if (id.empty() || id.size() > 128 || id[0] == '.' || id[0] == '-') return false;
  for (char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') return false;
  return true;
}

}  // namespace apron

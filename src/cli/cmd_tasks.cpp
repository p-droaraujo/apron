// apron tasks, and the bridge between runs and tasks.md.
#include <cstdio>

#include "cli/common.hpp"
#include "core/tasks.hpp"

namespace apron::cli {

namespace {

std::string glyph(const Style& st, char state, bool ready) {
  switch (state) {
    case '~': return st.blue("●");
    case '?': return st.amber("◐");
    case 'x': return "✓";
    case '-': return st.dim("–");
    default: return ready ? "○" : st.dim("◌");
  }
}

}  // namespace

bool sync_task(const Project& project, const std::string& id, char state, const std::string& by) {
  std::string err;
  auto file = TaskFile::load(project.tasks_file(), &err);
  if (!file || !file->find(id)) return false;
  file->set_state(id, state);
  file->set_field(id, kBy, state == '~' || state == '?' ? by : "");
  if (!file->save(project.tasks_file(), &err)) {
    std::fprintf(stderr, "%s\n", trf("warning: {}", {err}).c_str());
    return false;
  }
  return true;
}

int cmd_tasks(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  std::string err;
  auto file = TaskFile::load(project->tasks_file(), &err);
  if (!file) return fail(err);
  Style st;
  bool only_ready = args.has("ready") || args.has("prontas");
  std::string section;
  for (const auto& t : file->tasks()) {
    bool ready = t.state == ' ' && blockers_to_start(*file, t).empty();
    if (only_ready && !ready) continue;
    if (t.section != section) {
      section = t.section;
      std::printf("%s%s\n", section.empty() ? "" : "\n", st.dim(section).c_str());
    }
    std::string who = t.field(kBy);
    std::string deps;
    if (t.state == ' ' && !ready && !t.depends().empty()) {
      for (const auto& d : t.depends()) deps += (deps.empty() ? "" : ", ") + d;
      deps = st.dim("  ← " + deps);
    }
    std::printf("  %s %s %s%s%s\n", glyph(st, t.state, ready).c_str(), st.bold(t.id).c_str(), t.title.c_str(),
                who.empty() ? "" : st.dim("  " + who).c_str(), deps.c_str());
  }
  return 0;
}

}  // namespace apron::cli

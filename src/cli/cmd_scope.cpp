// apron scope [run]: what the run touched so far, against the task's scope.
#include <cstdio>

#include "cli/common.hpp"
#include "core/runs.hpp"

namespace apron::cli {

int cmd_scope(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  std::string run = args.pos.empty() ? "" : args.pos[0];
  if (run.empty()) {
    auto current = project->current_run();
    if (!current) return fail(tr("no run in progress; use apron scope <run>"));
    run = *current;
  }
  auto events = load_events(project->run_file(run));
  if (events.empty()) return fail(trf("run not found: {}", {run}));

  // A finished run keeps what it measured; a running one is measured now.
  std::vector<std::string> files, outside;
  const Event& last = events.back();
  if (last.kind == Kind::RunFinished && last.extra_find("files")) {
    for (const auto& v : last.extra_find("files")->arr()) files.push_back(v.str());
    if (const auto* o = last.extra_find("outside"))
      for (const auto& v : o->arr()) outside.push_back(v.str());
  } else {
    ScopeReport r = measure_scope(*project, events);
    files = r.files;
    outside = r.outside;
  }
  Style st;
  for (const auto& f : files) {
    bool out = false;
    for (const auto& o : outside) out = out || o == f;
    std::printf("  %s %s\n", out ? st.amber("✕").c_str() : st.dim("·").c_str(), out ? st.amber(f).c_str() : f.c_str());
  }
  std::printf("\n%s\n", trf("{} files, {} outside the scope", {std::to_string(files.size()), std::to_string(outside.size())}).c_str());
  return outside.empty() ? 0 : 1;
}

}  // namespace apron::cli

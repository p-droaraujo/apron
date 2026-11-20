// The bridge between runs and tasks.md: a run moves its task through [~], [?] and [x].
#include <cstdio>

#include "cli/common.hpp"
#include "core/tasks.hpp"

namespace apron::cli {

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


}  // namespace apron::cli

// apron projects: every project this machine's aprond has heard about.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "core/time.hpp"

namespace apron::cli {

int cmd_projects(const Args&) {
  DaemonClient c;
  std::string err;
  if (!c.connect(false, &err)) return fail(tr("aprond is not running; projects show up here once a run reports while it runs"));
  json::Object req;
  req.emplace_back("cmd", "projects.list");
  auto reply = c.request(std::move(req), &err);
  if (!reply) return fail(err);
  const json::Value* list = reply->find("projects");
  if (!list || list->arr().empty()) {
    std::puts(tr("no projects yet").c_str());
    return 0;
  }
  Style st;
  int64_t now = timeutil::now_ms();
  for (const auto& p : list->arr()) {
    auto count = [&](const char* key) { return p.find(key) ? static_cast<int>(p.find(key)->num()) : 0; };
    std::string states;
    if (int n = count("waiting")) states += st.amber("◐ " + std::to_string(n)) + "  ";
    if (int n = count("working")) states += st.blue("● " + std::to_string(n)) + "  ";
    if (int n = count("blocked")) states += st.dim("⊘ " + std::to_string(n)) + "  ";
    if (int n = count("done")) states += "✓ " + std::to_string(n) + "  ";
    std::string when = p.find("last") ? timeutil::short_duration(now - timeutil::parse_iso8601(p.get_str("last")).value_or(now)) : "";
    std::string root = p.get_str("root");
    if (const char* home = std::getenv("HOME"); home && root.rfind(home, 0) == 0) root = "~" + root.substr(std::strlen(home));
    std::printf("%s  %s%s  %s\n", st.bold(p.get_str("name")).c_str(), states.c_str(), st.dim(when).c_str(), st.dim(root).c_str());
    if (!p.get_str("last_msg").empty())
      std::printf("   %s %s\n", st.dim(p.get_str("last_task")).c_str(), p.get_str("last_msg").c_str());
  }
  return 0;
}

}  // namespace apron::cli

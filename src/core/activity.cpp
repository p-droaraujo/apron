#include "core/activity.hpp"

#include <algorithm>
#include <filesystem>

#include "core/project.hpp"

namespace apron {

std::vector<ActivityItem> load_activity(const std::vector<std::string>& roots, size_t limit) {
  std::vector<ActivityItem> out;
  for (const auto& root : roots) {
    Project p{root};
    std::string name = p.root.filename().string();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(p.runs_dir(), ec)) {
      if (entry.path().extension() != ".jsonl") continue;
      for (auto& e : load_events(entry.path())) out.push_back({name, std::move(e)});
    }
  }
  std::sort(out.begin(), out.end(), [](const ActivityItem& a, const ActivityItem& b) { return a.event.ts_ms > b.event.ts_ms; });
  if (out.size() > limit) out.resize(limit);
  return out;
}

}  // namespace apron

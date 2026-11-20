// Activity: the newest events of several projects, newest first.
// Used by the TUI for this machine and by aprond to answer `events.recent`.
#pragma once

#include <string>
#include <vector>

#include "core/events.hpp"

namespace apron {

struct ActivityItem {
  std::string project;  // the project's folder name
  Event event;
};

std::vector<ActivityItem> load_activity(const std::vector<std::string>& roots, size_t limit);

}  // namespace apron

#include "core/activity.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>

#include "core/project.hpp"

namespace apron {

namespace {

int64_t mtime_ms(const std::filesystem::path& file) {
  struct stat st {};
  if (::stat(file.c_str(), &st) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#else
  return static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
}

struct RunFile {
  int64_t mtime;
  std::filesystem::path path;
  const std::string* project;
};

}  // namespace

// A run file is written when its events happen, so no event in it is newer
// than the file itself. Reading files from the newest down, we can stop at the
// first file older than the oldest event we keep: usually a handful of files
// are parsed, however many runs the projects have.
std::vector<ActivityItem> load_activity(const std::vector<std::string>& roots, size_t limit) {
  std::vector<std::string> names;
  names.reserve(roots.size());
  for (const auto& root : roots) names.push_back(std::filesystem::path(root).filename().string());

  std::vector<RunFile> files;
  for (size_t i = 0; i < roots.size(); ++i) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(Project{roots[i]}.runs_dir(), ec))
      if (entry.path().extension() == ".jsonl") files.push_back({mtime_ms(entry.path()), entry.path(), &names[i]});
  }
  std::sort(files.begin(), files.end(), [](const RunFile& a, const RunFile& b) { return a.mtime > b.mtime; });

  auto newer = [](const ActivityItem& a, const ActivityItem& b) { return a.event.ts_ms > b.event.ts_ms; };
  std::vector<ActivityItem> out;
  for (const auto& f : files) {
    // A margin of a second for clocks and file systems that round.
    if (out.size() >= limit && f.mtime + 1000 < out.back().event.ts_ms) break;
    for (auto& e : load_events(f.path)) out.push_back({*f.project, std::move(e)});
    std::sort(out.begin(), out.end(), newer);
    if (out.size() > limit) out.resize(limit);
  }
  return out;
}

}  // namespace apron

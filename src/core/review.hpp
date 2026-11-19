// Review: what a person does with a finished run. Shared by `apron review`
// and the Review tab.
#pragma once

#include <string>
#include <vector>

#include "core/project.hpp"
#include "core/runs.hpp"

namespace apron {

// Finished runs that no one has reviewed yet: failing checks first, then scope
// drift, then the newest.
std::vector<RunSummary> review_queue(const Project& project);

struct RunChanges {
  std::string commit;  // the run's commit (Apron-Run trailer), empty if uncommitted
  std::string branch;  // a branch that holds it, e.g. apron/T2
  std::string stat;    // git's --stat of the run's change
};
RunChanges run_changes(const Project& project, const std::string& run);

// Each returns an empty string on success, or why it could not be done.
// merge: fast-forwards main to the run's branch; never merges a diverged main.
std::string review_merge(const Project& project, const std::string& run);
// return: reopens the task with the person's comment in its `review` field,
// where the next agent reads it.
std::string review_return(const Project& project, const std::string& run, const std::string& comment);
// discard: reopens the task; the branch stays, nothing is deleted.
std::string review_discard(const Project& project, const std::string& run);

}  // namespace apron

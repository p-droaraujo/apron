// apron review: the queue of finished runs, and what to do with each.
#include <cstdio>

#include "cli/common.hpp"
#include "core/review.hpp"
#include "core/time.hpp"

namespace apron::cli {

int cmd_review(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  Style st;
  if (args.pos.empty()) {
    auto queue = review_queue(*project);
    if (queue.empty()) {
      std::puts(tr("nothing to review").c_str());
      return 0;
    }
    for (const auto& r : queue) {
      std::string marks;
      if (r.checks_failed) marks += st.red("✕ checks") + " ";
      if (!r.outside.empty()) marks += st.amber("✕" + std::to_string(r.outside.size()) + " " + tr("outside")) + " ";
      std::printf("%s %s %s %s\n  %s\n", st.bold(r.task).c_str(), st.dim(r.id).c_str(), marks.c_str(),
                  st.dim(r.agent + "@" + r.machine).c_str(), r.last_msg.c_str());
    }
    return 0;
  }
  if (args.pos.size() < 2) return fail(tr("usage: apron review [<run> merge|return \"comment\"|discard]"));
  const std::string& run = args.pos[0];
  const std::string& action = args.pos[1];
  std::string err;
  if (action == "merge") err = review_merge(*project, run);
  else if (action == "return") err = review_return(*project, run, args.rest(2));
  else if (action == "discard") err = review_discard(*project, run);
  else return fail(tr("usage: apron review [<run> merge|return \"comment\"|discard]"));
  return err.empty() ? 0 : fail(err);
}

}  // namespace apron::cli

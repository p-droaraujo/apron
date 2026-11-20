// The sidebar's data: sessions and projects of this machine and of every
// machine in ~/.config/apron/machines, collected in the background so the
// screen never waits on ssh.
#pragma once

#include <memory>

#include "tui/app.hpp"

namespace apron::cli {

class FleetPoller {
 public:
  FleetPoller();
  ~FleetPoller();  // never waits for a slow ssh: the worker finishes on its own
  FleetPoller(const FleetPoller&) = delete;
  FleetPoller& operator=(const FleetPoller&) = delete;

  tui::Fleet snapshot() const;
  void poke();  // collect again now, after an action changed something
  // Fetch the board of `root` on `host` with every round; "" stops.
  void watch(const std::string& host, const std::string& root);

  struct State;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace apron::cli

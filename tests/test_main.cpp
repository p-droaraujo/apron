#include "test.hpp"

int main() {
  int run = 0;
  for (const auto& c : apron::test::registry()) {
    int before = apron::test::failures();
    c.fn();
    ++run;
    std::printf("%s %s\n", apron::test::failures() == before ? "ok  " : "FALHOU", c.name);
  }
  std::printf("\n%d testes, %d falhas\n", run, apron::test::failures());
  return apron::test::failures() == 0 ? 0 : 1;
}

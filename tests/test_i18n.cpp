#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "core/i18n.hpp"
#include "test.hpp"

using namespace apron;

TEST("i18n: English by default, Portuguese on request, English as fallback") {
  i18n::set(i18n::Lang::En);
  CHECK_EQ(tr("GATES"), std::string("GATES"));
  i18n::set(i18n::Lang::PtBr);
  CHECK_EQ(tr("GATES"), std::string("PORTÕES"));
  CHECK_EQ(trf("run not found: {}", {"r-1"}), std::string("execução não encontrada: r-1"));
  CHECK_EQ(tr("a string nobody translated"), std::string("a string nobody translated"));
  i18n::set(i18n::Lang::En);
  CHECK_EQ(trf("depends on {}, which is not done yet", {"T2"}), std::string("depends on T2, which is not done yet"));
}

// Every literal passed to tr() or trf() in src/ must have a Portuguese entry.
// This keeps the secondary language complete as the code grows.
TEST("i18n: every interface string has a Portuguese translation") {
  std::regex call(R"re(\btrf?\(\s*"((?:[^"\\]|\\.)*)")re");
  int missing = 0, seen = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(APRON_SOURCE_DIR "/src")) {
    if (!entry.is_regular_file()) continue;
    std::ifstream in(entry.path());
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it) {
      std::string key = (*it)[1].str();
      // Undo the escapes the regex captured literally.
      for (size_t p = 0; (p = key.find("\\\"", p)) != std::string::npos;) key.replace(p, 2, "\"");
      ++seen;
      if (!i18n::has_portuguese(key)) {
        ++missing;
        std::fprintf(stderr, "  missing translation: \"%s\" (%s)\n", key.c_str(), entry.path().filename().c_str());
      }
    }
  }
  CHECK(seen > 30);
  CHECK_EQ(missing, 0);
}

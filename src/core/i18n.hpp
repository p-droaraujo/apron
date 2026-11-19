// Interface language. English is the source and the default; every visible
// string is written in English in the code and looked up in a catalog.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace apron::i18n {

enum class Lang { En, PtBr };

// APRON_LANG wins; otherwise LC_ALL, LC_MESSAGES, LANG; otherwise English.
Lang detect();
Lang current();
void set(Lang lang);  // for tests

// True if the Portuguese catalog has this English string (used by tests).
bool has_portuguese(std::string_view english);

// Translate an English message into the current language.
std::string tr(std::string_view english);
// Translate, then replace each "{}" with the next argument.
std::string trf(std::string_view english, std::initializer_list<std::string> args);

}  // namespace apron::i18n

namespace apron {
using i18n::tr;
using i18n::trf;
}  // namespace apron

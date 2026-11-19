#include "core/json.hpp"
#include "test.hpp"

using namespace apron;

TEST("json: ida e volta preserva ordem e tipos") {
  json::Object o;
  o.emplace_back("b", 1);
  o.emplace_back("a", "texto com \"aspas\" e \\barra\n");
  o.emplace_back("ok", true);
  o.emplace_back("lista", json::Array{1, 2.5, nullptr});
  std::string text = json::dump(json::Value(o));
  CHECK_EQ(text, std::string(R"({"b":1,"a":"texto com \"aspas\" e \\barra\n","ok":true,"lista":[1,2.5,null]})"));
  auto back = json::parse(text);
  CHECK(back.has_value());
  CHECK_EQ(json::dump(*back), text);
}

TEST("json: acentos e escapes unicode") {
  auto v = json::parse(R"({"msg":"decis\u00e3o ◐","emoji":"\ud83d\ude80"})");
  CHECK(v.has_value());
  CHECK_EQ(v->get_str("msg"), std::string("decisão ◐"));
  CHECK_EQ(v->get_str("emoji"), std::string("\xF0\x9F\x9A\x80"));
}

TEST("json: entradas inválidas são recusadas com mensagem") {
  std::string err;
  CHECK(!json::parse("{\"a\":", &err));
  CHECK(!err.empty());
  CHECK(!json::parse("{\"a\":1} lixo"));
  CHECK(!json::parse("[1,]"));
  CHECK(!json::parse("\"sem fim"));
}

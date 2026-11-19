#include "core/tasks.hpp"
#include "test.hpp"

using namespace apron;

namespace {
const char* kSample = R"(# Tarefas

Texto livre que não pode mudar.

## v0a · Observar

- [x] T1 Eventos locais
  escopo: src/**
  pronto: o log mostra a execução

- [ ] T2 O tasks.md é lido
  escopo: src/**, tests/**
  depende: T1
  pronto: start recusa tarefa incompleta
  checks: build, test

- [ ] T3 Sem pronto
  escopo: src/**
  depende: T2

## v0b · Revisar

- [ ] T4 Depende de quem não existe
  escopo: src/**
  pronto: nunca
  depende: T1, T99
)";
}

TEST("tasks: reads ids, states, sections and fields") {
  auto f = TaskFile::from_text(kSample);
  CHECK_EQ(f.tasks().size(), size_t(4));
  const Task* t2 = f.find("T2");
  CHECK(t2 != nullptr);
  CHECK_EQ(t2->title, std::string("O tasks.md é lido"));
  CHECK_EQ(t2->section, std::string("v0a · Observar"));
  CHECK_EQ(t2->field("checks"), std::string("build, test"));
  CHECK_EQ(t2->field("scope"), std::string("src/**, tests/**"));  // written as escopo
  CHECK_EQ(t2->depends().size(), size_t(1));
  CHECK_EQ(f.find("T4")->section, std::string("v0b · Revisar"));
  CHECK(f.find("T1")->state == 'x');
}

TEST("tasks: text comes back identical when nothing changes") {
  auto f = TaskFile::from_text(kSample);
  CHECK_EQ(f.text(), std::string(kSample));
}

TEST("tasks: changing state and fields touches only the task's lines") {
  auto f = TaskFile::from_text(kSample);
  CHECK(f.set_state("T2", '~'));
  CHECK(f.set_field("T2", "by", "claude@nexus"));
  CHECK(f.find("T2")->state == '~');
  CHECK_EQ(f.find("T2")->field("by"), std::string("claude@nexus"));
  CHECK(f.text().find("  por: claude@nexus") != std::string::npos);  // keeps the task's language
  // The new field goes after the last field without breaking the next task.
  CHECK_EQ(f.find("T3")->field("depends"), std::string("T2"));
  CHECK(f.set_field("T2", "by", "codex@mac"));
  CHECK_EQ(f.find("T2")->field("by"), std::string("codex@mac"));
  CHECK(f.set_field("T2", "by", ""));
  CHECK(f.set_state("T2", ' '));
  CHECK_EQ(f.text(), std::string(kSample));
}

TEST("tasks: the minimum contract to pick up a task") {
  auto f = TaskFile::from_text(kSample);
  CHECK(blockers_to_start(f, *f.find("T2")).empty());
  auto t3 = blockers_to_start(f, *f.find("T3"));
  CHECK_EQ(t3.size(), size_t(2));  // missing done-when, and T2 is not done
  auto t4 = blockers_to_start(f, *f.find("T4"));
  CHECK_EQ(t4.size(), size_t(1));  // T99 does not exist
  CHECK(!blockers_to_start(f, *f.find("T1")).empty());  // already done
}

TEST("tasks: English keys work the same and are written in English") {
  auto f = TaskFile::from_text("- [ ] A1 Title\n  scope: src/**\n  done-when: it works\n  depends: —\n");
  const Task* a = f.find("A1");
  CHECK(a != nullptr);
  CHECK(!a->written_in_portuguese());
  CHECK(blockers_to_start(f, *a).empty());
  CHECK(f.set_field("A1", "by", "codex@mac"));
  CHECK(f.text().find("  by: codex@mac") != std::string::npos);
}

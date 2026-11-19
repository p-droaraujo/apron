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

TEST("tasks: lê ids, estados, seções e campos") {
  auto f = TaskFile::from_text(kSample);
  CHECK_EQ(f.tasks().size(), size_t(4));
  const Task* t2 = f.find("T2");
  CHECK(t2 != nullptr);
  CHECK_EQ(t2->title, std::string("O tasks.md é lido"));
  CHECK_EQ(t2->section, std::string("v0a · Observar"));
  CHECK_EQ(t2->field("checks"), std::string("build, test"));
  CHECK_EQ(t2->depends().size(), size_t(1));
  CHECK_EQ(f.find("T4")->section, std::string("v0b · Revisar"));
  CHECK(f.find("T1")->state == 'x');
}

TEST("tasks: o texto volta idêntico quando nada muda") {
  auto f = TaskFile::from_text(kSample);
  CHECK_EQ(f.text(), std::string(kSample));
}

TEST("tasks: mudar estado e campos só toca as linhas da tarefa") {
  auto f = TaskFile::from_text(kSample);
  CHECK(f.set_state("T2", '~'));
  CHECK(f.set_field("T2", "por", "claude@nexus"));
  CHECK(f.find("T2")->state == '~');
  CHECK_EQ(f.find("T2")->field("por"), std::string("claude@nexus"));
  // O campo novo entra depois do último campo, sem quebrar a próxima tarefa.
  CHECK_EQ(f.find("T3")->field("depende"), std::string("T2"));
  CHECK(f.set_field("T2", "por", "codex@mac"));
  CHECK_EQ(f.find("T2")->field("por"), std::string("codex@mac"));
  CHECK(f.set_field("T2", "por", ""));
  CHECK(f.set_state("T2", ' '));
  CHECK_EQ(f.text(), std::string(kSample));
}

TEST("tasks: o contrato mínimo para pegar uma tarefa") {
  auto f = TaskFile::from_text(kSample);
  CHECK(blockers_to_start(f, *f.find("T2")).empty());
  auto t3 = blockers_to_start(f, *f.find("T3"));
  CHECK_EQ(t3.size(), size_t(2));  // falta pronto, e T2 não está feita
  auto t4 = blockers_to_start(f, *f.find("T4"));
  CHECK_EQ(t4.size(), size_t(1));  // T99 não existe
  CHECK(!blockers_to_start(f, *f.find("T1")).empty());  // já feita
}

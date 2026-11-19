// O Andamento: o que existe no tasks.md e nos eventos, em quatro faixas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/project.hpp"
#include "core/runs.hpp"
#include "tui/screen.hpp"

namespace apron::tui {

enum class Lane { Plano, Executando, Portoes, Feito };

struct Item {
  Lane lane = Lane::Plano;
  std::string task;    // id da tarefa
  std::string title;
  std::string who;     // agente@máquina
  std::string detail;  // última frase, dependências ou motivo
  std::string run;     // id da execução, se houver
  RunState state = RunState::Working;
  bool ready = false;  // tarefa aberta com dependências feitas
  bool from_run = false;
  int64_t since_ms = 0;  // início da execução ou último evento
};

struct Model {
  std::string project;
  std::string machine;
  std::vector<Item> lanes[4];
  int waiting = 0, working = 0, blocked = 0;
};

Model load_model(const Project& project);

struct View {
  int lane = 1;     // faixa selecionada
  int index = 0;    // item selecionado na faixa
  bool help = false;
  int tick = 0;     // para a respiração de quem trabalha
};

void clamp_selection(const Model& model, View& view);
void render_andamento(Screen& screen, const Model& model, const View& view, int64_t now_ms);

// Linha do tempo de uma execução, com rolagem.
void render_log(Screen& screen, const Project& project, const std::string& run, int scroll, int64_t now_ms);
// Os campos de uma tarefa sem execução.
void render_task(Screen& screen, const Project& project, const std::string& task);

}  // namespace apron::tui

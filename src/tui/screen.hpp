// Grade de células com buffer duplo: só o que mudou vira sequência de escape,
// e o quadro inteiro vai dentro da saída sincronizada (modo 2026).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace apron::tui {

struct Color {
  uint32_t rgb = 0;
  bool set = false;  // false = cor padrão do terminal
  static Color none() { return {}; }
  static Color hex(uint32_t v) { return {v, true}; }
  bool operator==(const Color&) const = default;
};

struct Style {
  Color fg, bg;
  bool bold = false;
  bool operator==(const Style&) const = default;
};

struct Cell {
  std::string ch = " ";  // um ponto de código UTF-8
  Style style;
  bool operator==(const Cell&) const = default;
};

class Screen {
 public:
  void resize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }

  void clear();
  // Escreve texto a partir de (x, y), cortando no limite `max_w` (ou da tela).
  // Devolve quantas colunas usou.
  int put(int x, int y, std::string_view text, Style style, int max_w = -1);
  void fill(int x, int y, int w, std::string_view ch, Style style);
  const Cell& at(int x, int y) const { return back_[static_cast<size_t>(y * w_ + x)]; }

  // Gera os bytes que levam o terminal do quadro anterior a este.
  std::string flush();
  void invalidate() { full_ = true; }

 private:
  int w_ = 0, h_ = 0;
  std::vector<Cell> front_, back_;
  bool full_ = true;
};

// Quantas colunas um texto ocupa (um ponto de código = uma coluna).
int text_width(std::string_view text);
// Corta o texto em `w` colunas, com reticências se cortar.
std::string fit(std::string_view text, int w);

}  // namespace apron::tui

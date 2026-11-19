// O terminal de verdade: modo cru, tela alternativa, tamanho e teclas.
#pragma once

#include <string>

namespace apron::tui {

enum class Key { None, Up, Down, Left, Right, Enter, Esc, Tab, Char, Resize };

struct KeyEvent {
  Key key = Key::None;
  char ch = 0;
};

class Terminal {
 public:
  Terminal();   // entra no modo cru e na tela alternativa
  ~Terminal();  // restaura tudo, inclusive em saída por sinal
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  int width() const { return w_; }
  int height() const { return h_; }
  bool ok() const { return ok_; }

  // Espera até `timeout_ms` por uma tecla ou mudança de tamanho.
  KeyEvent poll(int timeout_ms);
  void write(const std::string& bytes);

 private:
  int w_ = 80, h_ = 24;
  bool ok_ = false;
  void query_size();
};

}  // namespace apron::tui

# Instruções para agentes

Este repositório é gerenciado pelo Apron, e o Apron é construído com ele mesmo. Siga o protocolo em `docs/agent-protocol.md`; o resumo está aqui.

## Antes de começar

1. Leia `.apron/tasks.md` e escolha uma tarefa `[ ]` cujas dependências estejam `[x]`.
2. Construa a CLI: `cmake -S . -B build -G Ninja && cmake --build build`.
3. Inicie a execução: `./build/apron run start <id> --agent <seu-nome> --intent "<o que vai fazer, em uma frase>"`.

## Enquanto trabalha

- A cada mudança de fase (ler, planejar, editar, testar, corrigir): `./build/apron report step "<o que mudou>"`.
- Precisa de uma decisão da pessoa (regra de negócio, voltado ao cliente, arquitetura, escopo)? `./build/apron report ask "<a pergunta>"` e espere.
- Algo de fora impede o avanço? `./build/apron report blocked --cause <gpu|budget|lock|machine|other> "<o motivo>"`.
- Aprendeu algo que a próxima execução deve saber? `./build/apron report note "<o aprendizado>"`.
- Fique dentro do `escopo` da tarefa. Precisa sair dele? Isso é um `ask`.

## Ao terminar

1. Rode os checks: `./build/apron check`.
2. Feche a execução: `./build/apron run finish --result done "<o que foi feito; o que ficou de fora; o que olhar primeiro>"`. Isso marca a tarefa `[x]` no `tasks.md`.
3. Faça o commit no branch da tarefa, com o `tasks.md` junto e o trailer `Apron-Run: <id da execução>`.

Uma frase por evento, no presente, dizendo o que mudou. O Apron mede sozinho arquivos, tempo e custo: não reporte isso.

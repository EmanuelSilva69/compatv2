# compatv2

Compilador / Analisador Sintático da linguagem **Tiny** com **recuperação em modo pânico**, versão em terminal e versão gráfica (GUI) animada em C99 sobre a **Raylib 5.5**.

O projeto é usado nas aulas de Compiladores para visualizar, passo a passo, o funcionamento do *scanner* e do *parser* descendente recursivo: fita de tokens, conjuntos PRIMEIROS/SEQUÊNCIA, gramática, justificativa didática de cada erro e recuperação em modo pânico com conjuntos de sincronização (anti-cascata).

## Estrutura

| Ficheiro | Descrição |
| --- | --- |
| `compilador_tiny.c` | Versão em terminal (CLI) — análise léxica/sintática com animação em cores ANSI |
| `gui_compilador.c` | Versão gráfica — IDE escura com editor, fita de tokens, consola em 4 abas e inspetor lateral |
| `wcag_check.py` | Verificação dos rácios de contraste WCAG 2.1 da paleta da GUI |
| `libraylib.*`, `libglfw3.dll.a`, `glfw3.dll` | Bibliotecas Raylib/GLFW locais (x86-64) para ligar em Windows |
| `raylib.h` | Cabeçalho da Raylib 5.5 |
| `openspec/` | Especificações e mudanças geridas com [OpenSpec](https://openspec.dev/) (workflow spec-driven) |
| `.opencode/`, `.vscode/` | Configuração do agente opencode e do VS Code (build/debug) |
| `*.pdf` | Enunciados e relatórios do trabalho |
| `*.png` | Capturas de ecrã dos testes/visualizações |

## Compilação

### Windows (MinGW-w64, GCC 64 bits — as libs Raylib desta pasta são x86-64)

```bash
gcc gui_compilador.c -I. -L. -lraylib -lopengl32 -lgdi32 -lwinmm -o gui_compilador.exe
gcc compilador_tiny.c -o compilador_tiny.exe
```

Ex.: `C:\Strawberry\c\bin\gcc.exe` ou `C:\msys64\ucrt64\bin\gcc.exe`
(o gcc i686 de `C:\mingw64\bin` não serve: as libs da pasta são de 64 bits).

### Linux

```bash
gcc gui_compilador.c -I. -L. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -o gui_compilador
gcc compilador_tiny.c -o compilador_tiny
```

### macOS

```bash
gcc gui_compilador.c -I. -L. -lraylib -lm -framework Cocoa -framework OpenGL \
    -framework IOKit -framework CoreVideo -o gui_compilador
gcc compilador_tiny.c -o compilador_tiny
```

## Modos de teste sem janela

```bash
gui_compilador --selftest           # roda os 5 exemplos e mostra o heap vivo (tem de ser 0)
gui_compilador --smoke 3            # abre a janela, analisa e fecha sozinho (3 s)
gui_compilador --input "<linha>"    # linha de partida
gui_compilador --shot saida.png     # guarda PNG quando a análise termina
gui_compilador --tab <0|1|2|3>      # Problemas / Consola / Gramática / Conjuntos
gui_compilador --no-insp            # arranca com o inspetor lateral recolhido
```

## Atalhos (GUI)

| Tecla | Ação |
| --- | --- |
| `F5` | Executar / Pausar |
| `F10` | Passo a passo |
| `F6` | Passo anterior |
| `F8` | Resetar |
| `ESC` | Sair |
| `ENTER` | Executa a análise a partir do campo de entrada |

## Arquitetura (`gui_compilador.c`, 4 secções)

1. **Secção 1** — tipos e dados: tokens, fita de eventos, tabelas formais de PRIMEIROS/SEQUÊNCIA.
2. **Secção 2** — motor: *lexer* + *parser* descendente recursivo com modo pânico (independente da Raylib; produz uma fita de eventos determinística).
3. **Secção 3** — máquina de estados de animação (View + Player) que consome os eventos e alimenta a justificativa didática.
4. **Secção 4** — interface Raylib (desenho, widgets, máquina de estados da UI).

Todo o *heap* passa por `xmalloc`/`xrealloc`/`xfree` com contadores de blocos e bytes — `--selftest` e a saída normal imprimem `[mem] ... blocos` (tem de ser `0`).

## Acessibilidade

```bash
python wcag_check.py
```

Valida que cada par cor de texto/fundo cumpre WCAG 2.1 (≥ 4.5:1 para texto, ≥ 3:1 para gráficos); termina com código `1` se algum par falhar.

## Workflow OpenSpec

As mudanças são planeadas com o workflow OpenSpec (proposal → design → specs → tasks → implementação → arquivo):

```bash
openspec list          # mudanças activas
openspec archive <id>  # arquivar mudança concluída
```

## Licença

Veja [LICENSE](LICENSE).

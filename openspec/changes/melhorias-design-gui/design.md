# Design

## Context

Ver proposal.md - Why. O motor (SEÇÃO 1–3 de `gui_compilador.c`) é intocado: ele gera uma **fita de eventos determinística** (`Analysis.ev`) que o player aplica em frente (`player_apply_next` → `view_apply`), e o estado visível é um *fold* do prefixo `ev[0..ev_next)`. A GUI (SEÇÃO 4) é imediata (raylib), com layout derivado de `layout_compute`, paleta em constantes (linhas 928–944), fita de tokens com scroll automático para o token foco (`ribbon_off`, 1706–1715), log em `v->log[][]`, e infra de aviso flutuante já existente (`banner/banner_hold` + `draw_toast`). Teclas usadas: F5/F10/F8; F1 e F6 estão livres.

## Goals / Non-Goals

**Goals:**
- Tornar verificável cada requisito das specs (`gui-acessibilidade`, `gui-controlo`, `gui-exploracao-didatica`) sem tocar no motor sintático.
- Manter build 0 avisos, `--selftest` 5/5 e as mensagens obrigatórias intactas.
- Reutilizar infra existente (ribbon scroll, banner/toast, fita de eventos) em vez de criar subsistemas novos.

**Non-Goals:**
- Alterar paleta base, layout em 4 blocos ou estética sóbria (apenas afinação de greys/contraste).
- Escala DPI, persistência entre sessões, edição multi-linha do código.
- Filtros avançados da consola ou visualização de árvore de análise.

## Decisions

**D1 - Passo anterior por replay determinístico.** Ao ativar o passo anterior (botão ◀ na topbar + `F6`): `view_reset` e reaplicar `view_apply` sobre `ev[0..ev_next-2]`, terminando com `ev_next` decrementado. *Alternativas rejeitadas*: (a) eventos inversos — lógica paralela propensa a divergir do *fold*; (b) snapshots por evento — memória × tempo de eventos, desnecessário (nev é dezenas/centenas, replay é O(n) < 1 ms). Desativar quando `ev_next == 0`. Durante replay repor `didactic_t/status_t/res_t` aos defaults (evita *fades* a meio).

**D2 - Contraste por constantes, não por sítios de uso.** `COL_MUTE` sobe de `#71717a` (≈2,9:1) para ≈`(152,152,161)` (≈5,2:1 sobre `COL_PANEL`, ≈5,0:1 sobre `COL_PANEL_IN`). Usos *textuais* de `COL_FOCUS` (ex.: nome do conjunto na caixa de detalhe, 2,6:1) passam a `COL_DIM` (≈5,6:1); `COL_FOCUS` mantém-se para barras/acentos não-textuais (≈3,2:1 ≥ 3:1 exigido a elementos gráficos). *Alternativa*: novo token `COL_FOCUS_T` — rejeitado por criar uma terceira cor "quase igual" e mais um erro de escolha futura.

**D3 - Movimento reduzido como multiplicador de animação.** Flag `ap->reduced_motion` (toggle no painel de ajuda + botão na topbar) que faz: (i) todos os fatores de suavização visuais `g_dt * k` passam a `g_dt * k * 4` via helper único `anim_k()`; (ii) amplitude de tremor = 0. O relógio do player (`acc`) **não** é escalado — só transições, nunca o ritmo da análise. *Alternativa*: escalar `g_dt` globalmente — rejeitado porque `g_dt` alimenta também o ritmo do player.

**D4 - Tooltip central desenhado no fim do frame.** Estado único em `App` (`tip_rect/tip_t/tip_text`); variantes de botão registam texto; a desenha-se **depois** de todos os painéis (última chamada antes do ecrã), uma caixa por vez, com atraso ~0,6 s. *Alternativa*: desenhar dentro do botão — rejeitado por problemas de z-order entre painéis.

**D5 - Painel de ajuda como overlay modal simples.** `ap->help_open`, desenhado por último; conteúdo estático: tabela de atalhos (F5/F10/F6/F8/F1/ESC/ENTER, Ctrl+C/X no campo, cliques na fita/Problemas) + legenda de estados da fita. Alterna com `F1` e fecha com `F1`/`ESC`; os restantes atalhos continuam ativos (previsibilidade). Enquanto aberto, o campo de entrada ignora caracteres.

**D6 - erro ↔ token via contagem de `EV_ERROR`.** Invariante já garantido em `report_error`: cada erro armado faz `ev_push(EV_ERROR)` **e** `msgs[]` na mesma ordem → `msgs[i]` ↔ i-ésimo `EV_ERROR` ↔ `e->tok`. Clique na aba Problemas resolve o índice do token por varrimento da fita, grava `ap->hl_tok`, e reutiliza o código existente de manter-o-visível (1706–1715) para deslocar `ribbon_off`; a fita desenha um anel pulsado em `hl_tok`. Limpeza de `hl_tok` em novo `analysis_build` e em seleção de outra mensagem.

**D7 - Cópia por botões no cabeçalho da aba ativa.** Botão junto ao distintivo de estado: Problemas → junta `a->msgs[]` com `\n`; Consola → junta `v->log[0..nlog)`. `SetClipboardText` com buffer estático `MAX_LOG*(LOG_LEN+1)` (ou `xmalloc`+`xfree` imediato se o cálculo exceder ~64 KB). Feedback reutiliza o `banner` existente: "copiado" / "nada para copiar". *Alternativa*: atalho só de teclado — rejeitado por não descobrível (motivo desta change).

**D8 - Indicadores de foco/hover.** Hover já unificado (`COL_HOVER`); acrescentar anel de foco consistente ao único controlo com foco de teclado (campo de entrada) e garantir que todos os botões partilham o mesmo realce. Sem navegação por Tab (novo subsistema, fora de âmbito).

**D9 - Toggle de movimento reduzido na topbar + subtítulo condicional.** O botão é só-de-ícone (onda vetorial = normal, linhas = reduzido; sem glifos raros) e o cluster de controlos da direita passa a ocupar ~736 px; em janelas < 1240 px o subtítulo é omitido para nunca colidir com o segmentado de velocidade. *Alternativa rejeitada*: esconder o próprio botão em janela estreita — a acessibilidade não pode depender da largura; *alternativa2*: tooltip obrigatório fica para a task 2.2 (infra de tooltips), pelo que a descoberta completa completa-se lá.

**D10 - Modo pânico: MATCH na marca esperada (exceção ao "motor intocado").** Correção pedida explicitamente pelo professor: em `var.:=temp`, o `:=` é a marca esperada pelo modo pânico mas não estava no conjunto de recuperação — era descartado, causando 2 erros artificiais. O `panic_sync` recebe agora `exp_mask` (tokens que devem fazer MATCH quando encontrados na varredura): `parser_atribuicao` recupera o `:=` e continua a atribuição (1 erro); `parser_read`/`parse_operando` recuperam o ID/operando. *Invariante mantida*: anti-cascata (`g_armado`) e `msgs[i]` ↔ i-ésimo `EV_ERROR`. Selftest caso 3: 20→18 eventos, 2→1 erro.

## Risks / Trade-offs

- [Subir `COL_MUTE` suaviza a hierarquia visual mute↔dim] → verificar rácios por script e manter diferença de luminância perceptível entre `MUTE` e `DIM` (152 vs 161 é pouco — escolher MUTE ≈152 e manter DIM 161; distinguir por peso/contexto, não só cor).
- [Replay do passo anterior re-aplica eventos com timers de animação a zero] → transientes de re-entra curtos são aceitáveis e preferíveis a lógica inversa; documentado em D1.
- [Clique em erro para token falha se `ntok` ou mensagens mudarem de contagem] → invariante 1:1 verificável no selftest (adicionar caso com 2 erros e checagem de mapeamento).
- [Overlay/tooltip a cobrir conteúdo em janela mínima (1120×660)] → limitar largura da caixa de ajuda e recalcular posição; tooltips acima/abaixo do cursor conforme espaço.
- [Alterar assinaturas de botões (tooltips) toca muitos call-sites] → fazer por variante nova (`ui_button_tip`) mantendo os call-sites antigos válidos até migração.

## Migration Plan

Rebuild local (`gcc … -Wall -Wextra`), `--selftest` e `--smoke` como gate; rollback = reverter os commits da change (app desktop, sem dados). Sem passos de deploy.

## Open Questions

(nenhum — tecla de passo anterior `F6` e localização dos botões de cópia estão decididos em D1/D7.)

# Proposal

## Why

A GUI é a entrega principal do trabalho e a única superfície que o aluno/avaliador vê; uma análise de design do estado atual identificou três problemas concretos: (1) contraste abaixo do WCAG AA em texto estrutural (`COL_MUTE` ≈ 2,9:1 em abas inativas e placeholders, `COL_FOCUS` como texto ≈ 2,6:1), (2) o player da análise só avança — não há passo anterior — e as funcionalidades secundárias (atalhos, botões de ícone, inspetor) não são descobríveis sem ler o código, e (3) os painéis não se ligam entre si: uma mensagem de erro não foca o token que a originou e o log/mensagens não são copiáveis para consulta fora da app.

## What Changes

- **Acessibilidade**: elevar o contraste do texto estrutural para ≥ 4,5:1 (ajuste de `COL_MUTE`/rotulos em `COL_FOCUS`), garantir que nenhum estado é comunicado só por cor (ícone/rótulo acompanha sempre a cor), e acrescentar uma opção de movimento reduzido que encurta as animações.
- **Controlo e descoberta**: passo anterior no player (botão + atalho) para rever eventos sem recomeçar; tooltips em botões de ícone e controlos menos óbvios; painel de ajuda com atalhos e glossário dos estados da fita (tecla `F1`/botão `?`).
- **Ligações didáticas entre painéis**: clicar numa mensagem na aba Problemas destaca/pulsa o token correspondente na fita de tokens; ação de copiar as mensagens de erro e o log da consola para a área de transferência.
- **Polish**: indicadores de foco/hover visíveis de forma consistente em todos os controlos interativos.

Fora do âmbito: qualquer alteração ao motor léxico/sintático, às mensagens obrigatórias, aos 5 casos do selftest, ao layout em 4 blocos e à paleta base (são apenas afinações de contraste, não uma repaleta).

## Capabilities

### New Capabilities

- `gui-acessibilidade`: requisitos de legibilidade e acessibilidade da interface (contraste mínimo, feedback não-exclusivamente-por-cor, controlo de movimento).
- `gui-controlo`: requisitos dos controlos de execução da análise e da sua descoberta (passo anterior, tooltips, ajuda de atalhos).
- `gui-exploracao-didatica`: requisitos de ligação entre painéis para estudo (erro ↔ token, exportação de mensagens/log).

### Modified Capabilities

(nenhuma — o projeto ainda não tem specs; esta change cria as primeiras)

## Impact

- **Código**: `gui_compilador.c` apenas — constantes de cor (928–944), player/topbar (1299–1400), aba Problemas + fita de tokens (1656–1900), novo painel de ajuda e tooltips na secção 4 (GUI).
- **Regressão esperada**: zero no motor — `--selftest` (5 casos) e `--smoke` têm de continuar a passar; mensagens obrigatórias intactas.
- **Dependências**: nenhuma nova (usa `SetClipboardText` já disponível na raylib).
- **Premissa registada**: "design" = design de interação/visual da GUI; o motor (SEÇÃO 1–3) não é alterado.

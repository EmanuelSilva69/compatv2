# Tasks

## 1. Acessibilidade

- [x] 1.1 Subir `COL_MUTE` para ≈(152,152,161) e trocar os usos *textuais* de `COL_FOCUS` por `COL_DIM` (barras/acentos mantêm `COL_FOCUS`); verificar com um script de cálculo WCAG (rácio de luminância das constantes da paleta) que todo o texto fica ≥ 4,5:1 e os elementos gráficos ≥ 3:1
- [x] 1.2 Revisar o desenho dos estados da fita (`ST_PENDING..ST_LEXERR`) e da lista de abas para garantir que cada estado tem sinal não-cromático (ícone, sublinhado ou rótulo); verificar por screenshot que estados distintos não dependem só da cor
- [x] 1.3 Acrescentar a flag `reduced_motion` + helper `anim_k()` (suavizações visuais ×4, tremor a 0, ritmo do player intocado) com toggle no topo; verificar que com a flag ativa as transições terminam ≤ ¼ do tempo e o `--smoke` corre sem alterar o número de eventos
- [x] 1.4 Gate do grupo: `gcc … -Wall -Wextra` a 0 avisos e `gui_compilador --selftest` a dar `== SELFTEST: TUDO OK ==`

## 2. Controlo e descoberta

- [ ] 2.1 Implementar o passo anterior (`act_step_back` por replay conforme design D1) com botão ◀ na topbar e atalho `F6`, inativo quando `ev_next == 0`; verificar na app: executar, retroceder, e o contador do rodapé diminui de um até ficar inativo no início
- [ ] 2.2 Infra de tooltip (estado central + desenho no fim do frame, atraso ~0,6 s) e variante `ui_button_tip`; registar tooltips em todos os botões só-de-ícone e controlos não óbvios; verificar: manter o cursor ~1 s sobre cada botão de ícone mostra a caixa com ação + atalho
- [ ] 2.3 Painel de ajuda (botão `?` + `F1`, fecha com `F1`/`ESC`) com tabela de atalhos e legenda de estados; campo de entrada ignora caracteres enquanto aberto; atualizar o cabeçalho do ficheiro (secção ATALHOS/MODOS) com `F1` e `F6`; verificar: `F1` alterna o painel e o cabeçalho documenta ambos os atalhos
- [ ] 2.4 Unificar hover (`COL_HOVER`) em todos os botões e acrescentar anel de foco visível ao campo de entrada; verificar por screenshot que foco e hover são distinguíveis do estado normal
- [ ] 2.5 Gate do grupo: build 0 avisos + `--selftest` 5/5 + `--smoke 9` fecha sem vazamentos

## 3. Ligações didáticas

- [ ] 3.1 Clique na aba Problemas → resolver o token pelo i-ésimo `EV_ERROR`, gravar `ap->hl_tok`, reutilizar o keep-visible da fita (1706–1715) e desenhar anel pulsado; limpar em novo análise/seleção; verificar: com `read .var1`, clicar numa mensagem destaca e traz à vista o token correspondente
- [ ] 3.2 Botões "copiar" no cabeçalho da aba ativa (Problemas → `a->msgs[]`, Consola → `v->log[]`) via `SetClipboardText`, feedback pelo `banner` existente ("copiado" / "nada para copiar"); verificar: copiar com 2 erros cola as 2 linhas exatas, copiar vazio mostra o aviso e não altera a área de transferência
- [ ] 3.3 Gate do grupo: build 0 avisos + `--selftest` 5/5 (invariante `msgs[i]` ↔ i-ésimo `EV_ERROR` mantida)

## 4. Verificação de integração

- [ ] 4.1 Build final `C:\Strawberry\c\bin\gcc.exe gui_compilador.c -o gui_compilador.exe -I. -L. -lraylib -lopengl32 -lgdi32 -lwinmm -Wall -Wextra` sem nenhum aviso
- [ ] 4.2 `--selftest` → `== SELFTEST: TUDO OK ==` com 0 bytes vivos e as mensagens obrigatórias intactas; `--smoke 9` → fecha com 0 vazamentos
- [ ] 4.3 Screenshots de regressão (`--shot` nas abas Problemas/Conjuntos + nova cena com tooltip/ajuda) verificados por amostragem de píxeis: contraste, sinal não-cromático e ausência de regressões visuais

## Workflow follow-up

- Depois de revistos, correr `/opsx-apply` para implementar e `/opsx-archive` no fim.

# Spec Delta

## Purpose

Garante que a interface é legível e utilizável por todos os utilizadores: contraste de texto mínimo WCAG AA, estado comunicado por mais do que cor, e controlo do movimento das animações.

## ADDED Requirements

### Requirement: Contraste mínimo do texto estrutural
Todo o texto renderizado pela interface (rótulos, abas inativas, placeholders, mensagens e nomes de conjuntos) SHALL ter contraste de luminância ≥ 4,5:1 face ao fundo sobre o qual é desenhado.

#### Scenario: Aba inativa legível
- **WHEN** a cor de texto de uma aba inativa da consola é aplicada sobre o painel
- **THEN** o rácio de contraste calculado entre o texto e o fundo é ≥ 4,5:1

#### Scenario: Placeholder do campo de entrada
- **WHEN** o texto de ajuda/placeholder do campo de entrada é desenhado
- **THEN** o rácio de contraste é ≥ 4,5:1 face ao fundo do campo

### Requirement: Estado não comunicado apenas por cor
Nenhum estado (erro, aceite, descartado, em análise, aba ativa, seleção) SHALL ser distinguido exclusivamente pela cor: cada estado MUST incluir também um ícone, símbolo, sublinhado ou rótulo de texto.

#### Scenario: Token em erro
- **WHEN** um token da fita está em estado de erro
- **THEN** para além da cor de erro, o token apresenta um sinal não-cromático (ícone ou sublinhado)

#### Scenario: Aba ativa
- **WHEN** uma aba da consola está selecionada
- **THEN** para além da cor do texto, está presente um indicador não-cromático (sublinhado) da aba ativa

### Requirement: Opção de movimento reduzido
A interface SHALL expor uma opção de movimento reduzido que, quando ativa, reduz para um quarto a duração de todas as animações e elimina os efeitos de tremor, mantendo a informação e o resultado visual de cada estado.

#### Scenario: Tremor no erro desativado
- **WHEN** o utilizador ativa o movimento reduzido e ocorre um token em erro
- **THEN** o cartão do token não executa o efeito de tremor e a transição de estado completa-se em ≤ 1/4 do tempo normal

#### Scenario: Análise continua legível
- **WHEN** o movimento reduzido está ativo durante a execução da análise
- **THEN** todos os eventos continuam a ser aplicados e mostrados, apenas com transições mais rápidas

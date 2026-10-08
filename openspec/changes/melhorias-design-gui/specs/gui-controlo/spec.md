# Spec Delta

## Purpose

Torna os controlos da análise completos e descobríveis: permite retroceder eventos já executados, explica botões e atalhos no próprio interface e disponibiliza uma ajuda agregada.

## ADDED Requirements

### Requirement: Passo anterior no player
O player SHALL permitir retroceder exatamente um evento da análise, restaurando o estado visual (fita de tokens, consola, inspetor, rodapé) correspondente ao ponto imediatamente anterior ao último evento aplicado; o controlo MUST estar inativo quando não há eventos anteriores.

#### Scenario: Retroceder um evento
- **WHEN** o utilizador ativa o passo anterior com pelo menos um evento já aplicado
- **THEN** o estado mostrado corresponde ao instante anterior ao último evento e o contador do rodapé diminui de um

#### Scenario: Início da análise
- **WHEN** nenhum evento foi ainda aplicado e o utilizador ativa o passo anterior
- **THEN** o controlo está inativo e o estado não altera

### Requirement: Tooltips em controlos de ícone
Todos os botões que usam apenas ícone ou significado não óbvio SHALL mostrar um tooltip ao manter o cursor sobre eles durante ~0,6 s, indicando a ação e o respetivo atalho de teclado, desaparecendo ao mover o cursor.

#### Scenario: Tooltip do botão de passo
- **WHEN** o utilizador mantém o cursor sobre o botão de passo avançado
- **THEN** surge um tooltip com o nome da ação e o respetivo atalho

### Requirement: Painel de ajuda de atalhos e estados
A interface SHALL disponibilizar um painel de ajuda (botão `?` e tecla `F1`) que lista os atalhos de teclado e o significado de cada estado da fita de tokens, abrindo e fechando de forma alternada pela mesma tecla ou `ESC`.

#### Scenario: Abrir ajuda
- **WHEN** o utilizador pressiona `F1`
- **THEN** um painel sobreposto mostra a lista de atalhos e a legenda de estados

#### Scenario: Fechar ajuda
- **WHEN** o painel de ajuda está aberto e o utilizador pressiona `F1` ou `ESC`
- **THEN** o painel fecha e o interface volta ao estado anterior

### Requirement: Indicadores de foco e hover consistentes
Todo o controlo interativo SHALL mostrar visualmente o seu estado de hover e, quando focado por teclado, um indicador de foco visível e distingüível do estado normal.

#### Scenario: Hover de botão
- **WHEN** o cursor entra na área de um botão
- **THEN** o botão apresenta o realce de hover já usado no resto da interface

#### Scenario: Foco por teclado
- **WHEN** um controlo recebe foco de teclado
- **THEN** é desenhado um indicador de foco perceptível

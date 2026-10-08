# Spec Delta

## Purpose

Liga os painéis entre si para estudo: cada mensagem de erro conduz ao token que a originou e o conteúdo didático (mensagens e log) pode ser exportado para consulta fora da aplicação.

## ADDED Requirements

### Requirement: Mensagem de erro foca o token correspondente
A aplicação SHALL permitir que a seleção de uma mensagem na aba Problemas destaque de forma proeminente o token da fita de tokens que a originou, deslocando a fita caso o token esteja fora da área visível, e o destaque MUST anular-se com a seleção de outra mensagem ou com a nova execução.

#### Scenario: Selecionar mensagem
- **WHEN** o utilizador clica numa mensagem de erro na aba Problemas
- **THEN** o token correspondente na fita recebe um destaque pulsado e a fita desloca-se para o mostrar

#### Scenario: Mensagem sem token correspondente
- **WHEN** a mensagem selecionada não corresponde a um token visível ou ainda não analisado
- **THEN** nenhum token é destacado e a interface não apresenta erro

### Requirement: Copiar mensagens de erro
A aplicação SHALL disponibilizar uma ação que copia para a área de transferência todas as mensagens de erro da análise corrente, uma por linha, preservando o texto exato mostrado na aba Problemas.

#### Scenario: Copiar com erros presentes
- **WHEN** o utilizador ativa a cópia de mensagens com 2 erros registados
- **THEN** a área de transferência contém as 2 linhas exatamente como mostradas

#### Scenario: Copiar sem erros
- **WHEN** o utilizador ativa a cópia de mensagens e a análise não tem erros
- **THEN** a área de transferência não é alterada e um aviso breve indica que não há mensagens

### Requirement: Copiar log da consola
A aplicação SHALL disponibilizar uma ação que copia para a área de transferência o conteúdo visível do log da aba Consola, preservando a ordem e o texto das linhas.

#### Scenario: Copiar log
- **WHEN** o utilizador ativa a cópia do log com eventos registados
- **THEN** a área de transferência contém as linhas do log pela ordem em que são mostradas

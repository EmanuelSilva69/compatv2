#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Calcula rácios de contraste WCAG 2.1 da paleta de gui_compilador.c.

Uso:  python wcag_check.py

  TEXTO   : rácio >= 4.5:1  (SC 1.4.3 - contraste de texto)
  GRAFICO : rácio >= 3.0:1  (SC 1.4.11 - objetos graficos informativos)

Cada par lista a cor (fg) e o fundo (bg) sobre o qual e desenhada, com a
origem na SECAO 4. Pares "MIX" amostram a interpolacao de mixc() usada em
texto (set_row). Saida com codigo 1 se algum par falhar.
"""
import re
import sys
import pathlib

SRC = pathlib.Path(__file__).with_name("gui_compilador.c")


def parse_palette(src):
    pal = {}
    for m in re.finditer(
        r"#define\s+(COL_\w+)\s+\(Color\)\{\s*(\d+),\s*(\d+),\s*(\d+),", src
    ):
        pal[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    if "COL_MUTE" not in pal:
        sys.exit("paleta nao encontrada em gui_compilador.c")
    return pal


def lum(c):
    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    r, g, b = (f(x) for x in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


src = SRC.read_text(encoding="utf-8")
P = parse_palette(src)

# cores derivadas (literals / misturas da SECAO 4)
PILL = mix(P["COL_HOVER"], (62, 62, 72), 0.6)                    # seletor segmentado
D = {
    "PILL": PILL,
    "BTN_PRIM": P["COL_FOCUS_D"],
    "CARD_NEUTRAL": (30, 30, 36),
    "CARD_DISCARD": (42, 42, 46),
    "CARD_SCANNED": (35, 41, 54),
    "PINK": (254, 205, 205),
    "TOAST_BG": (46, 26, 26),
    "TRACK": (63, 63, 70),
}
ALL = dict(P, **D)


def rgb(name):
    return ALL[name]


# (fg, bg, origem)  -- texto desenhado sobre o fundo indicado
TEXT_PAIRS = [
    ("COL_TEXT", "COL_BG", "texto geral / rodape"),
    ("COL_TEXT", "COL_BG2", "abas (ativas), barra de cima"),
    ("COL_TEXT", "COL_PANEL", "titulo de painel, log, listas"),
    ("COL_TEXT", "COL_PANEL_IN", "botao, campo, cartoes, conjuntos"),
    ("COL_TEXT", "COL_HOVER", "rotulo de botao em hover"),
    ("COL_TEXT", "COL_FILLOK", "corpo do cartao aceite"),
    ("COL_TEXT", "COL_FILLERR", "corpo do cartao de erro"),
    ("COL_TEXT", "CARD_NEUTRAL", "corpo do cartao pendente"),
    ("COL_TEXT", "CARD_DISCARD", "corpo do cartao descartado"),
    ("COL_TEXT", "CARD_SCANNED", "corpo do cartao lido"),
    ("COL_TEXT", "PILL", "aba ativa do seletor segmentado"),
    ("COL_TEXT", "TOAST_BG", "aviso flutuante"),
    ("COL_TEXT", "BTN_PRIM", "rotulo do botao primario"),
    ("PINK", "COL_FILLERR", "txtc dos cartoes de erro"),
    ("PINK", "TOAST_BG", "texto do aviso flutuante"),
    ("COL_DIM", "COL_BG", "apoio geral"),
    ("COL_DIM", "COL_BG2", "abas inativas (hover), sublinhado"),
    ("COL_DIM", "COL_PANEL", "cabecalhos, legendas, notas, log kind 2"),
    ("COL_DIM", "COL_PANEL_IN", "inspetor, tipo do token, campo"),
    ("COL_DIM", "COL_HOVER", "segmento inativo em hover"),
    ("COL_DIM", "CARD_NEUTRAL", "rotulo pendente"),
    ("COL_DIM", "CARD_SCANNED", "rotulo 'lido' (era COL_FOCUS)"),
    ("COL_DIM", "PILL", "segmento inativo"),
    ("COL_MUTE", "COL_BG", "dica da fita vazia"),
    ("COL_MUTE", "COL_BG2", "abas inativas, segmentos inativos"),
    ("COL_MUTE", "COL_PANEL", "'fonte:', '^ rolar', meta do inspetor"),
    ("COL_MUTE", "COL_PANEL_IN", "placeholder, botao desativado"),
    ("COL_MUTE", "CARD_DISCARD", "txtc/labelc descartado"),
    ("COL_MUTE", "CARD_NEUTRAL", "cartao pendente"),
    ("COL_GREEN", "COL_PANEL", "cabecalho OK, log OK, badge OK"),
    ("COL_GREEN", "COL_PANEL_IN", "valor de conjunto em uso"),
    ("COL_GREEN", "COL_FILLOK", "rotulo do cartao aceite"),
    ("COL_RED_T", "COL_PANEL", "cabecalho de erros, log ERR, badge"),
    ("COL_RED_T", "COL_PANEL_IN", "apoio de erro em painel interior"),
    ("COL_RED_T", "COL_FILLERR", "rotulo do cartao de erro"),
]

# (fg, bg, origem) -- objetos graficos informativos (barras, aneis, amostras)
GRAPH_PAIRS = [
    ("COL_FOCUS", "COL_BG", "anel de foco do campo, barra de progresso"),
    ("COL_FOCUS", "COL_BG2", "anel de foco sobre barra superior"),
    ("COL_FOCUS", "COL_PANEL", "acentos, scanner, moldura, progresso"),
    ("COL_FOCUS", "COL_PANEL_IN", "aneis, barras de linha, circulos"),
    ("COL_MUTE", "COL_PANEL", "amostra 'descartado', borda do cartao"),
    ("COL_MUTE", "COL_PANEL_IN", "borda do cartao descartado"),
    ("COL_MUTE", "COL_BG2", "amostra sobre barra de abas"),
    ("COL_MUTE", "CARD_DISCARD", "borda do cartao descartado"),
    ("COL_GREEN", "COL_PANEL", "borda/marca de sucesso, amostra"),
    ("COL_GREEN", "COL_PANEL_IN", "barra de linha em uso"),
    ("COL_GREEN", "COL_FILLOK", "borda do cartao aceite"),
    ("COL_GREEN", "BTN_PRIM", "pista do botao primario"),
    ("COL_DIM", "COL_BG", "borda do botao primario (hover) vs fundo"),
    ("COL_GREEN_D", "COL_PANEL", "borda de cartao aceite"),
    ("COL_GREEN_D", "COL_PANEL_IN", "borda de cartao aceite"),
    ("COL_GREEN_D", "COL_FILLOK", "borda do cartao aceite"),
    ("COL_RED", "COL_PANEL", "marcas X de erro, amostra, borda do toast"),
    ("COL_RED", "COL_PANEL_IN", "sublinhado ondulado"),
    ("COL_RED", "COL_FILLERR", "borda do cartao de erro"),
    ("COL_RED_D", "COL_PANEL", "borda do cartao lex-erro"),
    ("COL_RED_D", "COL_PANEL_IN", "borda do cartao lex-erro"),
    ("COL_RED_D", "COL_FILLERR", "borda do cartao lex-erro"),
    ("COL_DIM", "TRACK", "thumb da barra de scroll (era COL_FOCUS)"),
    ("COL_DIM", "COL_PANEL", "thumb da barra de scroll"),
]

# (fg_a, fg_b, bg, origem) -- misturas de mixc() usadas como texto
MIX_PAIRS = [
    ("COL_DIM", "COL_GREEN", "COL_PANEL", "valor do conjunto (set_row)"),
    ("COL_DIM", "COL_GREEN", "COL_PANEL_IN", "valor do conjunto (set_row)"),
    ("COL_DIM", "COL_TEXT", "COL_PANEL_IN", "nome do conjunto (set_row)"),
]

fails = 0
rows = 0


def report(kind, fg, bg, r, lo, origem):
    global fails, rows
    rows += 1
    ok = r >= lo
    if not ok:
        fails += 1
    mark = "OK  " if ok else "FAIL"
    print(
        f"  [{mark}] {kind:6s} {fg:>13s} vs {bg:<14s} {r:5.2f}:1 "
        f"(min {lo:.1f})  {origem}"
    )


print("== WCAG 2.1 - contraste da paleta de gui_compilador.c ==")
print("\nTEXTO (>= 4.5:1):")
for fg, bg, origem in TEXT_PAIRS:
    report("TEXTO", fg, bg, ratio(rgb(fg), rgb(bg)), 4.5, origem)

print("\nGRAFICO (>= 3.0:1):")
for fg, bg, origem in GRAPH_PAIRS:
    report("GRAFICO", fg, bg, ratio(rgb(fg), rgb(bg)), 3.0, origem)

print("\nMISTURAS DE TEXTO (>= 4.5:1, amostrado):")
for a, b, bg, origem in MIX_PAIRS:
    worst = min(
        ratio(mix(rgb(a), rgb(b), t / 10.0), rgb(bg)) for t in range(11)
    )
    report("MIX", f"{a}->{b}", bg, worst, 4.5, origem)

gap = abs(lum(P["COL_DIM"]) - lum(P["COL_MUTE"]))
print(
    f"\ninfo: separacao de luminancia MUTE vs DIM = {gap:.4f} "
    f"(MUTE {rgb('COL_MUTE')}, DIM {rgb('COL_DIM')})"
)
print(f"\n{rows} pares verificados, {fails} falha(s)")
sys.exit(1 if fails else 0)

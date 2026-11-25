"""Diagrams for SatRAR_Project_Slides.md. Labels follow tests/fixtures/paper/fig1.aag."""

from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

plt.rcParams["font.sans-serif"] = ["Microsoft JhengHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

OUT = Path(__file__).resolve().parent
NAVY = "#1F4E79"
TEAL = "#1A6B6B"
GOLD = "#B86E00"
RED = "#A33B32"
GREEN = "#1E7A46"
INK = "#1C2833"
MUTED = "#5D6D7E"
BG = "#F7F5F0"
CARD = "#FFFFFF"
LINE = "#D5D8DC"


def canvas(w, h):
    fig, ax = plt.subplots(figsize=(w, h), dpi=160)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis("off")
    return fig, ax


def box(ax, x, y, w, h, text, fc, ec=None, size=11, color="white", weight="medium", radius=0.08):
    patch = FancyBboxPatch(
        (x, y), w, h,
        boxstyle=f"round,pad=0.02,rounding_size={radius}",
        facecolor=fc, edgecolor=ec or fc, linewidth=1.2,
    )
    ax.add_patch(patch)
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
            fontsize=size, color=color, fontweight=weight, linespacing=1.25)


def arrow(ax, x1, y1, x2, y2, color=NAVY):
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=12,
        linewidth=1.4, color=color, shrinkA=0, shrinkB=0,
    ))


def save(fig, name):
    fig.savefig(OUT / name, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def architecture():
    fig, ax = canvas(13.2, 6.2)
    layers = [
        (4.95, "CommandShell", "read    fault_ma    rar_l2    rar_apply", NAVY),
        (3.75, "rar/", "SatRarEngine    Rewriter    RarVerifier    ExportAag", TEAL),
        (2.55, "analysis/", "GraphAnalysis    FaultContext    CircuitPropagator", GOLD),
        (1.35, "aig/  +  sat/", "AAG 電路          CNF / miter          MiniSat    CaDiCaL", "#3D4F63"),
    ]
    for y, title, body, color in layers:
        box(ax, 0.35, y, 2.5, 0.95, title, color, size=13, weight="bold")
        box(ax, 3.05, y, 9.7, 0.95, body, CARD, ec=color, size=13, color=INK)
        if y < 4.95:
            arrow(ax, 1.6, y + 1.2, 1.6, y + 0.95, MUTED)
    box(ax, 0.35, 0.2, 6.0, 0.7, "scripts/   BENCH 轉 AAG、批次、分片", "#EAF2F8", ec=NAVY, size=13, color=INK)
    box(ax, 6.55, 0.2, 6.2, 0.7, "results/   TSV、checkpoint、PHASE_STATUS", "#EAF2F8", ec=NAVY, size=13, color=INK)
    save(fig, "architecture.png")


def pipeline():
    fig, ax = canvas(13.2, 3.6)
    steps = [
        (0.25, "1  讀入 AAG", NAVY),
        (2.35, "2  pin 故障\n與 dominator", TEAL),
        (4.45, "3  BCP 求 MA", GOLD),
        (6.55, "4  trial\nwire / gate", "#6B4C2A"),
        (8.65, "5  先加入\n再移除", RED),
        (10.75, "6  兩段 CEC\n再匯出", GREEN),
    ]
    for i, (x, text, color) in enumerate(steps):
        box(ax, x, 1.15, 1.9, 1.35, text, color, size=12, weight="bold")
        if i:
            arrow(ax, x - 0.18, 1.82, x, 1.82, MUTED)
    ax.text(0.3, 0.55, "N0 原電路    →    N1 已加 alternative、目標 pin 還在    →    N2 目標 pin 接到 stuck 值",
            fontsize=12, color=INK)
    save(fig, "pipeline.png")


def fig1_circuit():
    fig, ax = canvas(13.4, 6.8)
    ax.text(0.3, 6.45, "Figure 1    紅：要變成冗餘的 pin    綠：candidate[3] 要加的線",
            fontsize=15, color=INK, fontweight="bold")

    def gate(x, y, name, expr, fc, ec):
        box(ax, x, y, 2.05, 0.85, f"{name}   {expr}", fc, ec=ec, size=12, color=INK, radius=0.06)

    gate(0.3, 4.7, "g3", "a  AND  b", "#FFF8EC", GOLD)
    gate(0.3, 3.4, "g1", "b  AND  d", "#FFF8EC", GOLD)
    gate(0.3, 2.1, "g2", "e  AND  NOT c", "#FFF8EC", GOLD)
    gate(3.2, 4.7, "g7", "g6  AND  g3", "#FDEDEC", RED)
    gate(3.2, 3.4, "g4", "g1  AND  c", "#FFF8EC", GOLD)
    gate(3.2, 2.1, "g6", "g2  OR  d", "#FDEDEC", RED)
    gate(6.2, 4.15, "g8", "g7  OR  g4", "#EAF6F1", GREEN)
    gate(6.2, 2.1, "g5", "g1  OR  g2", "#EAF2F8", NAVY)
    gate(9.2, 4.15, "g9", "g8  AND  f", "#EAF6F1", GREEN)
    box(ax, 11.6, 4.28, 1.3, 0.6, "PO  g9", "#EAF6F1", ec=GREEN, size=13, color=GREEN, weight="bold")
    box(ax, 8.6, 0.7, 1.3, 0.6, "PO  g5", "#EAF2F8", ec=NAVY, size=13, color=NAVY, weight="bold")

    arrow(ax, 5.25, 2.5, 5.25, 4.7, RED)
    arrow(ax, 5.25, 5.1, 6.2, 4.6, RED)
    arrow(ax, 5.25, 3.8, 6.2, 4.4)
    arrow(ax, 8.25, 4.55, 9.2, 4.55, GREEN)
    arrow(ax, 8.25, 2.5, 9.2, 1.0, NAVY)
    arrow(ax, 10.5, 4.55, 11.6, 4.55, GREEN)
    ax.annotate(
        "",
        xy=(9.2, 4.85), xytext=(8.25, 2.7),
        arrowprops=dict(arrowstyle="-|>", color=GREEN, lw=2.0, linestyle="dashed"),
    )
    ax.text(3.2, 5.65, "目標  var 13  pin 0  sa1", fontsize=12, color=RED, fontweight="bold")
    ax.text(9.2, 5.15, "attach = 15", fontsize=12, color=GREEN, fontweight="bold")
    ax.text(8.15, 3.35, "g5 接到 g9", fontsize=12, color=GREEN, fontweight="bold")
    ax.text(0.3, 1.15, "g6 的輸出接到 g7 的 pin 0。candidate[3] 的 source_raw=23 就是 g5。",
            fontsize=13, color=INK)
    save(fig, "fig1_circuit.png")


def rewrite():
    fig, ax = canvas(13.2, 5.6)
    ax.text(0.35, 5.2, "rar_apply 實際改了什麼", fontsize=16, color=INK, fontweight="bold")
    box(ax, 0.3, 2.55, 3.7, 2.2, "N0  原電路\n\ng9 直接到 PO\ng6 仍驅動 g7", CARD, ec=NAVY, size=14, color=INK)
    box(ax, 4.75, 2.55, 3.8, 2.2, "N1  加入\n\n新 gate\ng9' = g9  AND  g5\n\ng6→g7 還在", "#EAF6F1", ec=GREEN, size=14, color=INK)
    box(ax, 9.3, 2.55, 3.55, 2.2, "N2  移除\n\npin 接到 1\n再化簡\n\n功能不變", "#FDEDEC", ec=RED, size=14, color=INK)
    arrow(ax, 4.05, 3.65, 4.7, 3.65, GREEN)
    arrow(ax, 8.6, 3.65, 9.25, 3.65, RED)
    ax.text(0.4, 1.7, "attach=15 是 g9，被包起來的那個輸出", fontsize=14, color=INK)
    ax.text(0.4, 1.05, "source_raw=23 是 g5，AND 上去的那條 literal", fontsize=14, color=INK)
    ax.text(0.4, 0.4, "rar_l2 13 0 sa1 是 g7 的 pin 0，最後變成冗餘的那根線", fontsize=14, color=INK)
    save(fig, "fig1_rewrite.png")


def wire(ax, x1, y1, x2, y2, color, width=2.2, label=None, label_color=None, dy=0.18):
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=14,
        linewidth=width, color=color, shrinkA=2, shrinkB=2,
    ))
    if label:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + dy, label,
                ha="center", va="bottom", fontsize=12, color=label_color or color, fontweight="bold")


def redundant_target():
    fig, ax = canvas(13.2, 5.8)
    ax.text(0.35, 5.4, "冗餘目標線", fontsize=18, color=RED, fontweight="bold")
    ax.text(3.3, 5.45, "g6 進 g7 的 pin 0。這根線才是 rar_l2 要拿掉的對象。", fontsize=13, color=INK)

    box(ax, 0.4, 3.35, 2.1, 1.05, "g3\na  AND  b", "#FFF8EC", ec=GOLD, size=14, color=INK)
    box(ax, 0.4, 1.35, 2.1, 1.05, "g6\ng2  OR  d", "#FDEDEC", ec=RED, size=14, color=INK)
    box(ax, 6.3, 2.15, 2.4, 1.5, "g7\nvar 13", "#FDEDEC", ec=RED, size=16, color=INK, weight="bold")
    box(ax, 10.0, 2.35, 2.5, 1.15, "g8 → g9 → PO", "#EAF6F1", ec=GREEN, size=14, color=INK)

    wire(ax, 2.5, 3.85, 6.3, 3.25, MUTED, 1.8, "pin 1", MUTED, 0.12)
    wire(ax, 2.5, 1.9, 6.3, 2.55, RED, 3.4, "冗餘目標線", RED, 0.16)
    wire(ax, 8.7, 2.9, 10.0, 2.9, GREEN, 1.8)

    ax.text(6.35, 1.7, "pin 0    sa1", fontsize=13, color=RED, fontweight="bold")
    ax.text(0.4, 0.45, "命令 rar_l2 13 0 sa1。故障只改這一支 pin。通過後這根線改接到常數 1，不再影響 PO。",
            fontsize=13, color=INK)
    save(fig, "redundant_target.png")


def candidate_wiring():
    fig, ax = canvas(13.4, 6.8)
    ax.text(0.35, 6.4, "candidate[3] 接線", fontsize=18, color=GREEN, fontweight="bold")
    ax.text(4.4, 6.45, "source 是 g5，接到 g9 的輸出上，不是接到目標 pin。", fontsize=13, color=INK)

    ax.text(0.4, 5.85, "加入前", fontsize=14, color=NAVY, fontweight="bold")
    box(ax, 0.35, 4.35, 1.7, 0.85, "g8", "#EAF6F1", ec=GREEN, size=14, color=INK)
    box(ax, 0.35, 3.15, 1.7, 0.85, "f", "#E8EEF5", ec=NAVY, size=14, color=NAVY, weight="bold")
    box(ax, 3.0, 3.7, 2.0, 1.05, "g9\nattach 15", "#EAF6F1", ec=GREEN, size=13, color=INK)
    box(ax, 5.7, 3.9, 1.15, 0.7, "PO", "#EAF6F1", ec=GREEN, size=13, color=GREEN, weight="bold")
    wire(ax, 2.05, 4.75, 3.0, 4.35, MUTED, 1.6)
    wire(ax, 2.05, 3.55, 3.0, 4.05, MUTED, 1.6)
    wire(ax, 5.0, 4.22, 5.7, 4.22, GREEN, 1.8)

    ax.text(7.3, 5.85, "加入後  N1", fontsize=14, color=GREEN, fontweight="bold")
    box(ax, 7.2, 4.45, 1.45, 0.75, "g9", "#EAF6F1", ec=GREEN, size=13, color=INK)
    box(ax, 7.2, 3.25, 1.45, 0.75, "g5", "#EAF2F8", ec=NAVY, size=13, color=NAVY, weight="bold")
    box(ax, 9.5, 3.7, 2.15, 1.15, "新 AND\ng9' = g9  AND  g5", "#EAF6F1", ec=GREEN, size=12, color=INK, weight="bold")
    box(ax, 12.05, 3.9, 1.05, 0.7, "PO", "#EAF6F1", ec=GREEN, size=13, color=GREEN, weight="bold")
    wire(ax, 8.65, 4.82, 9.5, 4.45, GREEN, 1.8)
    wire(ax, 8.65, 3.62, 9.5, 4.05, GREEN, 2.6, "g5", GREEN, 0.08)
    wire(ax, 11.65, 4.25, 12.05, 4.25, GREEN, 1.8)
    ax.text(7.2, 2.85, "source_raw = 23", fontsize=12, color=NAVY, fontweight="bold")

    ax.text(0.4, 2.15, "目標線此時還在", fontsize=14, color=RED, fontweight="bold")
    box(ax, 0.35, 0.7, 1.8, 0.9, "g6", "#FDEDEC", ec=RED, size=14, color=INK)
    box(ax, 4.3, 0.7, 2.3, 0.9, "g7 pin 0", "#FDEDEC", ec=RED, size=14, color=INK)
    box(ax, 8.3, 0.7, 4.6, 0.9, "N2 才改接常數 1", "#FDEDEC", ec=RED, size=14, color=INK)
    wire(ax, 2.15, 1.15, 4.3, 1.15, RED, 3.0, "冗餘目標線", RED, 0.12)
    wire(ax, 6.6, 1.15, 8.3, 1.15, RED, 1.8)
    save(fig, "candidate_wiring.png")


def example_flow():
    fig, ax = canvas(13.2, 6.6)
    rows = [
        ("1", "讀入 fig1.aag", "9 顆 AND。PI 是 a–f。PO 是 g5 與 g9。", NAVY),
        ("2", "標出目標 pin", "g7（變數 13）pin 0、stuck-at-1。只改 g6 那一條輸入。", RED),
        ("3", "目標側 MA", "要偵測這個故障，良好電路必須有的值。其中有 g6=0、g5=0。", GOLD),
        ("4", "沿 dominator 走", "下游 output fault。g9 是其中一個。共用前綴留著。", TEAL),
        ("5", "L2 遞迴學習", "深度 2 才把 g9 側的相反 g5 值補進 MA。L0 沒有這筆。", "#6B4C2A"),
        ("6", "發出這條 wire", "兩邊 g5 相反。印成 candidate[3]，rule=opposite-MA。", GREEN),
        ("7", "改線並證明", "g9' = g9  AND  g5，pin 接到 1。CaDiCaL 證明兩段都等價。", NAVY),
    ]
    y = 5.85
    for num, title, body, color in rows:
        box(ax, 0.25, y, 0.55, 0.7, num, color, size=14, weight="bold", radius=0.1)
        ax.text(1.05, y + 0.48, title, fontsize=13, color=color, fontweight="bold", va="center")
        ax.text(4.35, y + 0.35, body, fontsize=12, color=INK, va="center")
        y -= 0.82
    save(fig, "fig1_flow.png")


if __name__ == "__main__":
    architecture()
    pipeline()
    fig1_circuit()
    redundant_target()
    candidate_wiring()
    rewrite()
    example_flow()

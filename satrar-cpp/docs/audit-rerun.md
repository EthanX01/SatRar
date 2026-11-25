# 稽核重跑備忘

詳見 `docs/SatRAR_Core_Consistency_Audit_2026-11-24.md`。

## 常數 miter smoke

- 電路 A：`aag 0 0 0 1 0` + 常數 0 PO
- 電路 B：常數 1 PO
- 期望 miter **SAT**

## PI 映射

- 兩電路同 PI 數、內部 AAG 編號不同仍等價 → **UNSAT**
- 同 var ID、PI 順序對調 PO → **SAT**

## Figure 1

Fixture：`tests/fixtures/paper/fig1.aag`

```text
read tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1
rar_wire 13 0 sa1
```

稽核時五模式 **0 候選**；獨立窮舉仍支持論文 wire/gate 兩段等
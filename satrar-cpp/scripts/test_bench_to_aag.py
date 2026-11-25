"""Independent checks of scripts/bench_to_aag.py (run: python -m unittest scripts/test_bench_to_aag.py).

The reference evaluator below interprets BENCH directly and never calls the converter; the AAG side is
read back by a separate tiny AIGER interpreter. Every primary output is compared for every PI vector.
"""

import itertools
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bench_to_aag import convert  # noqa: E402

OPS = {
    "AND": lambda v: all(v), "NAND": lambda v: not all(v),
    "OR": lambda v: any(v), "NOR": lambda v: not any(v),
    "XOR": lambda v: sum(v) % 2 == 1, "XNOR": lambda v: sum(v) % 2 == 0,
    "NOT": lambda v: not v[0], "BUF": lambda v: v[0], "BUFF": lambda v: v[0],
}


def parse_bench(text):
    inputs, outputs, gates = [], [], {}
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.upper().startswith("INPUT"):
            inputs.append(line[line.index("(") + 1:line.index(")")].strip())
        elif line.upper().startswith("OUTPUT"):
            outputs.append(line[line.index("(") + 1:line.index(")")].strip())
        else:
            name, rhs = [part.strip() for part in line.split("=", 1)]
            kind = rhs[:rhs.index("(")].strip().upper()
            gates[name] = (kind, [a.strip() for a in rhs[rhs.index("(") + 1:rhs.rindex(")")].split(",")])
    return inputs, outputs, gates


def bench_eval(parsed, assignment, extra_inputs=()):
    inputs, outputs, gates = parsed
    value = dict(zip(inputs + list(extra_inputs), assignment))
    value.update({"0": False, "1": True, "gnd": False, "vdd": True})

    def get(name):
        if name not in value:
            kind, args = gates[name]
            value[name] = OPS[kind]([get(a) for a in args])
        return value[name]

    return [get(o) for o in outputs]


def aag_eval(aag_text, assignment):
    lines = aag_text.splitlines()
    _, m, i, l, o, a = lines[0].split()
    i, o, a = int(i), int(o), int(a)
    assert int(l) == 0
    value = {0: False}
    for k in range(i):
        value[int(lines[1 + k]) // 2] = assignment[k]
    outputs = [int(x) for x in lines[1 + i:1 + i + o]]
    for row in lines[1 + i + o:1 + i + o + a]:
        z, x, y = (int(t) for t in row.split())

        def lit(raw):
            return value[raw // 2] != bool(raw & 1)
        value[z // 2] = lit(x) and lit(y)
    return [value[raw // 2] != bool(raw & 1) for raw in outputs]


def check_equivalent(test, text, cut=False, extra=()):
    aag, _ = convert(text, cut)
    parsed = parse_bench(text)
    n = len(parsed[0]) + len(extra)
    for assignment in itertools.product([False, True], repeat=n):
        test.assertEqual(aag_eval(aag, assignment), bench_eval(parsed, assignment, extra), assignment)


class ConverterTests(unittest.TestCase):
    def test_all_supported_gates_and_arities(self):
        for kind in ["AND", "NAND", "OR", "NOR", "XOR", "XNOR"]:
            for arity in (2, 3, 4):
                names = [f"i{k}" for k in range(arity)]
                text = "\n".join(f"INPUT({n})" for n in names) + f"\nOUTPUT(z)\nz = {kind}({', '.join(names)})\n"
                check_equivalent(self, text)
        for kind in ["NOT", "BUF", "BUFF"]:
            check_equivalent(self, f"INPUT(a)\nOUTPUT(z)\nz = {kind}(a)\n")

    def test_reordered_ports_shared_nets_and_constants(self):
        text = ("INPUT(b)\nINPUT(a)\nINPUT(c)\nOUTPUT(y)\nOUTPUT(x)\nOUTPUT(b)\n"
                "n1 = NAND(a, b)\nn2 = XOR(n1, c)\nx = NOR(n2, n1)\ny = AND(a, a)\n")
        check_equivalent(self, text)
        # A constant operand and the same net on both pins of a gate.
        check_equivalent(self, "INPUT(a)\nOUTPUT(z)\nOUTPUT(w)\nz = AND(a, 1)\nw = XOR(a, a)\n")

    def test_dff_cut_exposes_q_as_pi_and_d_as_po(self):
        text = "INPUT(a)\nOUTPUT(z)\nq = DFF(d)\nd = XOR(a, q)\nz = NAND(q, a)\n"
        aag, mapping = convert(text, cut_latches=True)
        self.assertEqual(mapping["cut_latches"], ["q"])
        self.assertEqual(mapping["inputs"], ["a", "q"])
        self.assertEqual(mapping["outputs"], ["z", "d"])
        parsed = parse_bench("INPUT(a)\nOUTPUT(z)\nOUTPUT(d)\nd = XOR(a, q)\nz = NAND(q, a)\n")
        for assignment in itertools.product([False, True], repeat=2):
            self.assertEqual(aag_eval(aag, assignment), bench_eval(parsed, assignment, ["q"]))

    def test_sequential_without_explicit_cut_is_rejected(self):
        with self.assertRaises(ValueError):
            convert("INPUT(a)\nOUTPUT(z)\nq = DFF(a)\nz = BUF(q)\n", cut_latches=False)

    def test_hard_errors(self):
        bad = {
            "undefined net": "INPUT(a)\nOUTPUT(z)\nz = AND(a, missing)\n",
            "undefined output": "INPUT(a)\nOUTPUT(nowhere)\nz = NOT(a)\n",
            "cycle": "INPUT(a)\nOUTPUT(z)\nz = AND(a, y)\ny = OR(z, a)\n",
            "truncated statement": "INPUT(a)\nOUTPUT(z)\nz = AND(a,",
            "no outputs": "INPUT(a)\nz = NOT(a)\n",
            "unsupported gate": "INPUT(a)\nOUTPUT(z)\nz = MUX(a, a, a)\n",
            "duplicate driver": "INPUT(a)\nOUTPUT(z)\nz = NOT(a)\nz = BUF(a)\n",
            "duplicate input": "INPUT(a)\nINPUT(a)\nOUTPUT(z)\nz = NOT(a)\n",
        }
        for name, text in bad.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    convert(text)

    def test_topology_metadata(self):
        text = ("INPUT(a)\nINPUT(b)\nINPUT(c)\nOUTPUT(z)\nOUTPUT(w)\n"
                "g1 = AND(a, b)\ng2 = NOT(g1)\nz = OR(g2, c)\nw = XOR(a, c)\ng3 = NAND(a, b, c)\n")
        aag, mapping = convert(text)
        self.assertEqual(mapping["original_wires"], 2 + 1 + 2 + 2 + 3)
        self.assertEqual(mapping["original_gate_arity"], {"AND/2": 1, "NAND/3": 1, "NOT/1": 1, "OR/2": 1, "XOR/2": 1})
        counts = mapping["pin_mapping_counts"]
        self.assertEqual(sum(counts.values()), mapping["original_wires"])
        self.assertEqual(counts["absorbed_no_aig_pin"], 1)
        self.assertEqual(mapping["aig_wires"], 2 * mapping["aig_and_gates"])
        self.assertEqual(mapping["aig_and_gates"], int(aag.split()[5]))
        self.assertEqual(mapping["original_input_count"], 3)
        self.assertEqual(mapping["original_output_count"], 2)


if __name__ == "__main__":
    unittest.main()

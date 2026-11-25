
"""Convert a gate-level BENCH network to combinational ASCII AIGER."""

import re
from collections import defaultdict, deque

# Bump when the conversion changes; it is part of every experiment checkpoint key.
CONVERTER_VERSION = "bench_to_aag-2"


def convert(text, cut_latches=False):
    inputs, outputs, definitions = [], [], {}
    for index, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        port = re.fullmatch(r"(INPUT|OUTPUT)\s*\(\s*([^(),\s]+)\s*\)", line, re.I)
        if port:
            (inputs if port[1].upper() == "INPUT" else outputs).append(port[2])
            continue
        gate = re.fullmatch(r"([^\s=]+)\s*=\s*(?:(\w+)\s*\(([^)]*)\)|([^\s(),]+))", line)
        if not gate:
            raise ValueError(f"line {index}: unsupported BENCH statement {line!r}")
        if gate[1] in definitions:
            raise ValueError(f"line {index}: duplicate driver {gate[1]!r}")
        operands = [part.strip() for part in (gate[3] or "").split(",")] if gate[2] else [gate[4]]
        definitions[gate[1]] = (gate[2].upper() if gate[2] else "BUF", operands)
    if len(set(inputs)) != len(inputs) or any(name in definitions for name in inputs):
        raise ValueError("duplicate primary input or driver")
    if not inputs or not outputs or not definitions:
        raise ValueError("BENCH has no INPUT, OUTPUT or gate statements (truncated file?)")

    latch_names = [name for name, (kind, _) in definitions.items() if kind == "DFF"]
    if latch_names and not cut_latches:
        raise ValueError("sequential BENCH requires explicit cut_latches:true")
    latch_inputs = []
    for name in latch_names:
        _, values = definitions.pop(name)
        if len(values) != 1:
            raise ValueError(f"expected one data input for DFF {name}")
        latch_inputs.append(values[0])
    input_ports = inputs + latch_names
    output_ports = outputs + latch_inputs
    original_wires = sum(len(args) for _, args in definitions.values()) + len(latch_names)

    signals = {name: 2 * (i + 1) for i, name in enumerate(input_ports)}
    for name, value in {"0": 0, "1": 1, "gnd": 0, "vdd": 1, "GND": 0, "VDD": 1}.items():
        if name not in definitions:
            signals.setdefault(name, value)
    records = []

    def and_two(x, y):
        raw = 2 * (len(input_ports) + len(records) + 1)
        records.append((raw, max(x, y), min(x, y)))
        return raw

    def fold(op, operands):
        acc = operands[0]
        for operand in operands[1:]:
            acc = op(acc, operand)
        return acc

    def or_two(x, y):
        return and_two(x ^ 1, y ^ 1) ^ 1

    def xor_two(x, y):
        return or_two(and_two(x, y ^ 1), and_two(x ^ 1, y))

    pending = {}
    users = defaultdict(list)
    ready = deque()
    for name, (_, operands) in definitions.items():
        missing = []
        for operand in operands:
            if operand in signals:
                continue
            if operand not in definitions:
                raise ValueError(f"{name}: undefined driver {operand!r}")
            missing.append(operand)
            users[operand].append(name)
        pending[name] = len(missing)
        if not missing:
            ready.append(name)

    while ready:
        name = ready.popleft()
        kind, operands = definitions[name]
        values = [signals[operand] for operand in operands]
        if kind in {"BUF", "BUFF", "NOT"} and len(values) == 1:
            result = values[0] ^ (kind == "NOT")
        elif kind in {"AND", "NAND", "OR", "NOR", "XOR", "XNOR"}:
            op = {"AND": and_two, "NAND": and_two, "OR": or_two, "NOR": or_two,
                  "XOR": xor_two, "XNOR": xor_two}[kind]
            result = fold(op, values) ^ (kind in {"NAND", "NOR", "XNOR"})
        else:
            raise ValueError(f"{name}: unsupported BENCH gate or arity {kind}/{len(values)}")
        signals[name] = result
        for user in users[name]:
            pending[user] -= 1
            if pending[user] == 0:
                ready.append(user)
    if any(pending.values()):
        raise ValueError("combinational cycle after DFF cut")
    if any(name not in signals for name in output_ports):
        raise ValueError("undefined BENCH output")

    aag = [f"aag {len(records) + len(input_ports)} {len(input_ports)} 0 {len(output_ports)} {len(records)}"]
    aag.extend(str(signals[name]) for name in input_ports)
    aag.extend(str(signals[name]) for name in output_ports)
    aag.extend(f"{z} {x} {y}" for z, x, y in records)
    aag.extend(f"i{i} {name}" for i, name in enumerate(input_ports))
    aag.extend(f"o{i} {name}" for i, name in enumerate(output_ports))
    gate_arity = defaultdict(int)
    pin_classes = {"one_to_one_up_to_pin_order": 0, "decomposed_one_to_many": 0, "absorbed_no_aig_pin": 0}
    for kind, args in definitions.values():
        gate_arity[f"{kind}/{len(args)}"] += 1
        if kind in {"BUF", "BUFF", "NOT"}:
            pin_classes["absorbed_no_aig_pin"] += len(args)
        elif kind in {"AND", "NAND", "OR", "NOR"} and len(args) == 2:
            pin_classes["one_to_one_up_to_pin_order"] += 2
        else:
            pin_classes["decomposed_one_to_many"] += len(args)
    return "\n".join(aag) + "\n", {
        "converter_version": CONVERTER_VERSION,
        "topology_profile": "aig",
        "original_wires": original_wires,
        # Source definition of original_wires: sum of gate fanin pin counts plus one edge per cut DFF
        # data input. Primary-output ports are not counted.
        "original_wires_definition": "gate fanin pins + cut DFF data edges; PO ports excluded",
        "original_gate_arity": dict(sorted(gate_arity.items())),
        "original_input_count": len(inputs),
        "original_output_count": len(outputs),
        "original_gate_count": len(definitions),
        "aig_and_gates": len(records),
        "aig_wires": 2 * len(records),
        # The AIG stores each 2-input AND with the larger literal on pin 0, so source pin order is not
        # kept even for the one-to-one class.
        "pin_order": "larger-literal-first",
        "pin_mapping_counts": pin_classes,
        "cut_latches": latch_names,
        "latch_cut_order": latch_names,
        "inputs": input_ports,
        "outputs": output_ports,
        "signals": signals,
    }

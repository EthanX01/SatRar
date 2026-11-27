"""Convert combinational ASCII AIGER (.aag) to binary AIGER (.aig).

Renumbers primary inputs in file order and AND gates in topological file order.
AND inputs are stored in the AIGER delta encoding (larger literal first).
Symbol names are canonical (i0, i1, ..., o0, o1, ...) so two files of one
equivalence job match by name and by order. Latches are rejected.

This is the reference implementation for the C++ AigWriter in
docs/SatRAR_ABC_CEC_Plan.md.
"""

import argparse
import sys
from pathlib import Path


def structural_lines(lines):
    """Number lines of the body. SatRAR writes one positive literal per primary input."""
    body = []
    for line in lines[1:]:
        if not line or line[0] in "ciol":
            if line.startswith("c"):
                break
            continue
        body.append(line)
    return body


def parse_aag(text):
    lines = text.splitlines()
    if not lines or not lines[0].startswith("aag "):
        raise ValueError("not an ASCII AIGER file")
    header = lines[0].split()
    if len(header) < 6:
        raise ValueError("AIGER header needs M I L O A")
    inputs, latches, outputs, ands = (int(v) for v in header[2:6])
    if latches != 0:
        raise ValueError("latches are not supported")
    body = structural_lines(lines)
    and_at = next((i for i, line in enumerate(body) if len(line.split()) >= 3), len(body))
    if ands == 0 and and_at == inputs + outputs:
        input_at = 0
    elif and_at == outputs:
        input_at = None  # standard AIGER: inputs are variables 1..I
    elif and_at == inputs + outputs:
        input_at = 0
    else:
        raise ValueError(
            f"body does not match standard AIGER or the SatRAR input-literal dialect "
            f"(first AND at {and_at}, I={inputs}, O={outputs})"
        )
    if input_at is None:
        input_lits = [2 * (index + 1) for index in range(inputs)]
        cursor = 0
    else:
        input_lits = [int(body[i]) for i in range(inputs)]
        if any(lit < 2 or lit & 1 for lit in input_lits):
            raise ValueError("primary input literals must be positive and nonconstant")
        cursor = inputs
    output_lits = [int(body[cursor + i]) for i in range(outputs)]
    cursor += outputs
    gates = []
    for i in range(ands):
        lhs, right, left = (int(v) for v in body[cursor + i].split()[:3])
        gates.append((lhs, right, left))
    return input_lits, output_lits, gates


def renumber(input_lits, output_lits, gates):
    """Map the source variables onto 1..I for inputs and I+1.. for AND gates."""
    inputs = len(input_lits)
    new_of = {lit >> 1: index for index, lit in enumerate(input_lits, start=1)}

    def mapped(lit):
        if lit <= 1:
            return lit
        variable = lit >> 1
        if variable not in new_of:
            raise ValueError(f"literal {lit} is used before its gate is defined")
        return (new_of[variable] << 1) | (lit & 1)

    encoded = []
    for index, (lhs, right, left) in enumerate(gates):
        variable = lhs >> 1
        if lhs != variable * 2:
            raise ValueError(f"AND output literal {lhs} is inverted")
        new_var = inputs + index + 1
        new_of[variable] = new_var
        fanin = sorted((mapped(right), mapped(left)), reverse=True)
        encoded.append((new_var * 2, fanin[0], fanin[1]))
    return [mapped(lit) for lit in output_lits], encoded


def encode_delta(value):
    data = bytearray()
    while value & ~0x7F:
        data.append((value & 0x7F) | 0x80)
        value >>= 7
    data.append(value)
    return data


def write_aig(path, input_lits, output_lits, gates):
    inputs = len(input_lits)
    outputs, encoded = renumber(input_lits, output_lits, gates)
    ands = len(encoded)
    maximum = inputs + ands
    with path.open("wb") as out:
        out.write(f"aig {maximum} {inputs} 0 {len(outputs)} {ands}\n".encode("ascii"))
        for lit in outputs:
            out.write(f"{lit}\n".encode("ascii"))
        for lhs, larger, smaller in encoded:
            if lhs <= larger or larger < smaller:
                raise ValueError("AND literal order is not topological")
            out.write(encode_delta(lhs - larger))
            out.write(encode_delta(larger - smaller))
        for index in range(inputs):
            out.write(f"i{index} i{index}\n".encode("ascii"))
        for index in range(len(outputs)):
            out.write(f"o{index} o{index}\n".encode("ascii"))
        out.write(b"c\nsatrar\n")
    return maximum, inputs, len(outputs), ands


def read_delta(data, cursor):
    value = 0
    shift = 0
    while True:
        if cursor >= len(data):
            raise ValueError("truncated delta")
        byte = data[cursor]
        cursor += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, cursor
        shift += 7


def decode_aig(path):
    data = path.read_bytes()
    newline = data.index(b"\n")
    header = data[:newline].decode("ascii").split()
    if header[0] != "aig" or len(header) < 6:
        raise ValueError("not a binary AIGER file")
    maximum, inputs, latches, outputs, ands = (int(v) for v in header[1:6])
    if latches != 0 or maximum != inputs + ands:
        raise ValueError("unexpected binary header")
    cursor = newline + 1
    output_lits = []
    for _ in range(outputs):
        end = data.index(b"\n", cursor)
        output_lits.append(int(data[cursor:end]))
        cursor = end + 1
    gates = []
    for index in range(ands):
        lhs = 2 * (inputs + index + 1)
        delta0, cursor = read_delta(data, cursor)
        delta1, cursor = read_delta(data, cursor)
        larger = lhs - delta0
        smaller = larger - delta1
        gates.append((lhs, larger, smaller))
    return inputs, output_lits, gates


def check_roundtrip(source, binary, invert_output=None):
    input_lits, outputs, gates = parse_aag(source.read_text(encoding="utf-8"))
    if invert_output is not None:
        outputs = list(outputs)
        outputs[invert_output] ^= 1
    expected_outputs, expected_gates = renumber(input_lits, outputs, gates)
    got_inputs, got_outputs, got_gates = decode_aig(binary)
    if got_inputs != len(input_lits) or got_outputs != expected_outputs or got_gates != expected_gates:
        raise SystemExit(f"round-trip mismatch: {source}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--check", action="store_true", help="decode the written file and compare")
    parser.add_argument("--invert-output", type=int, help="flip this output literal (0-based) before writing")
    args = parser.parse_args()
    input_lits, outputs, gates = parse_aag(args.source.read_text(encoding="utf-8"))
    if args.invert_output is not None:
        outputs = list(outputs)
        outputs[args.invert_output] ^= 1
    stats = write_aig(args.destination, input_lits, outputs, gates)
    if args.check:
        check_roundtrip(args.source, args.destination, args.invert_output)
    print("aig M=%d I=%d O=%d A=%d %s" % (*stats, args.destination))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate required ONNX tensor names, types, and shapes without dependencies."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def read_varint(data: bytes, offset: int) -> tuple[int, int]:
    value = shift = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, offset
        shift += 7
        if shift > 70:
            break
    raise ValueError("malformed protobuf varint")


def fields(data: bytes):
    offset = 0
    while offset < len(data):
        tag, offset = read_varint(data, offset)
        number, wire = tag >> 3, tag & 7
        if number == 0:
            raise ValueError("invalid protobuf field number zero")
        if wire == 0:
            value, offset = read_varint(data, offset)
        elif wire == 1:
            value, offset = data[offset : offset + 8], offset + 8
        elif wire == 2:
            length, offset = read_varint(data, offset)
            value, offset = data[offset : offset + length], offset + length
            if len(value) != length:
                raise ValueError("truncated protobuf length-delimited field")
        elif wire == 5:
            value, offset = data[offset : offset + 4], offset + 4
        else:
            raise ValueError(f"unsupported protobuf wire type {wire}")
        yield number, wire, value


def tensor_info(value_info: bytes) -> tuple[str, int, list[int]]:
    name = ""
    type_proto = b""
    for number, wire, value in fields(value_info):
        if number == 1 and wire == 2:
            name = value.decode("utf-8")
        elif number == 2 and wire == 2:
            type_proto = value
    tensor_proto = next((value for number, wire, value in fields(type_proto) if number == 1 and wire == 2), b"")
    elem_type = None
    shape_proto = b""
    for number, wire, value in fields(tensor_proto):
        if number == 1 and wire == 0:
            elem_type = value
        elif number == 2 and wire == 2:
            shape_proto = value
    shape: list[int] = []
    for number, wire, dimension in fields(shape_proto):
        if number != 1 or wire != 2:
            continue
        dim_value = next((value for field, kind, value in fields(dimension) if field == 1 and kind == 0), None)
        if dim_value is None:
            raise ValueError(f"dynamic dimension in output {name}")
        shape.append(dim_value)
    if not name or elem_type is None or not shape:
        raise ValueError("ONNX value info is missing tensor name, type, or shape")
    return name, elem_type, shape


def interface(path: Path) -> tuple[dict[str, tuple[int, list[int]]], dict[str, tuple[int, list[int]]]]:
    model = path.read_bytes()
    graph = next((value for number, wire, value in fields(model) if number == 7 and wire == 2), None)
    if graph is None:
        raise ValueError(f"{path.name} has no graph")
    inputs: dict[str, tuple[int, list[int]]] = {}
    outputs: dict[str, tuple[int, list[int]]] = {}
    for number, wire, value in fields(graph):
        if number in (11, 12) and wire == 2:
            name, dtype, shape = tensor_info(value)
            (inputs if number == 11 else outputs)[name] = (dtype, shape)
    return inputs, outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("prism-model-assets.json"))
    parser.add_argument("--models", type=Path, default=Path("out/prism-runtime/models/prism"))
    parser.add_argument("--describe", action="store_true", help="print observed model input and output names, types, and shapes")
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        expected_models = manifest["onnx_contracts"]
        if args.describe:
            description = {}
            for name in expected_models:
                description[name] = dict(zip(("inputs", "outputs"), interface(args.models / name)))
            print(json.dumps(description, indent=2))
            return 0
        type_codes = {"float32": 1, "int64": 7, "bool": 9}
        for filename, contracts in expected_models.items():
            found_inputs, found_outputs = interface(args.models / filename)
            for direction, found in (("inputs", found_inputs), ("outputs", found_outputs)):
                expected = contracts[direction]
                wanted_names = set(expected)
                if set(found) != wanted_names:
                    raise ValueError(
                        f"{filename} {direction} names must be {sorted(wanted_names)}; got {sorted(found)}"
                    )
                for name, contract in expected.items():
                    actual = found[name]
                    wanted = (type_codes[contract["dtype"]], contract["shape"])
                    if actual != wanted:
                        raise ValueError(
                            f"{filename} {direction[:-1]} {name!r} must be {contract['dtype']} "
                            f"{contract['shape']}; got {actual}"
                        )
        print("All pinned PRISM ONNX model inputs and outputs match the declared tensor contract.")
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f"PRISM ONNX contract validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

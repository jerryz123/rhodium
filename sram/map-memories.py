#!/usr/bin/env python3
# Plans selected CIRCT memory sites onto interface-described SRAM macros and emits exact-name wrappers.
# SPDX-License-Identifier: Apache-2.0

import argparse
import configparser
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


class MappingError(Exception):
    """Reports an unsupported memory contract or malformed mapper input."""


@dataclass(frozen=True)
class Port:
    direction: str
    name: str
    mlir_type: str
    width: Optional[int]


@dataclass(frozen=True)
class Memory:
    module: str
    ports: Tuple[Port, ...]
    attributes: Dict[str, str]
    depth: int
    width: int
    mask_granularity: int
    site: Optional[str] = None
    requested_macro: Optional[str] = None
    source_module: Optional[str] = None

    @property
    def port_map(self) -> Dict[str, Port]:
        return {port.name: port for port in self.ports}

    @property
    def mask_width(self) -> Optional[int]:
        port = self.port_map.get("RW0_wmask")
        return None if port is None else port.width


@dataclass(frozen=True)
class Macro:
    name: str
    interface: str
    depth: int
    width: int
    write_granularity: int
    address_width: int
    read_ports: int
    read_write_ports: int
    width_um: float
    height_um: float
    collateral: Dict[str, str]
    power_pins: Tuple[str, ...]
    functional_verilog: Optional[Path]

    @property
    def mask_width(self) -> int:
        return self.width // self.write_granularity

    @property
    def area_um2(self) -> float:
        return self.width_um * self.height_um


@dataclass(frozen=True)
class Plan:
    memory: Memory
    macro: Macro
    depth_banks: int
    width_slices: int

    @property
    def instance_count(self) -> int:
        return self.depth_banks * self.width_slices


_GENERATED_RE = re.compile(
    r'^\s*hw\.module\.generated(?:\s+private)?\s+@(?P<module>"(?:\\.|[^"])+"|[-A-Za-z0-9_.$]+)\s*,\s*'
    r'@FIRRTLMem\s*\((?P<ports>.*)\)\s+attributes\s*\{(?P<attributes>.*)\}\s*$'
)
_SELECTED_EXTERN_RE = re.compile(
    r'^\s*hw\.module\.extern(?:\s+private)?\s+@(?P<module>"(?:\\.|[^"])+"|[-A-Za-z0-9_.$]+)\s*'
    r'\((?P<ports>.*)\)\s+attributes\s*\{(?P<attributes>.*)\}\s*$'
)
_PORT_RE = re.compile(r"^(in|out)\s+%?([-A-Za-z0-9_.$]+)\s*:\s*(.+)$")
_INTEGER_RE = re.compile(r"^(-?[0-9]+)\s*:\s*[us]?i[0-9]+$")
_BIT_TYPE_RE = re.compile(r"^i([0-9]+)$")


def split_top_level(text: str) -> List[str]:
    """Splits comma-separated MLIR syntax while respecting nested delimiters."""
    parts: List[str] = []
    start = 0
    stack: List[str] = []
    closing = {"(": ")", "[": "]", "{": "}", "<": ">"}
    quote = False
    escaped = False
    for index, character in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quote = False
            continue
        if character == '"':
            quote = True
        elif character in closing:
            stack.append(closing[character])
        elif stack and character == stack[-1]:
            stack.pop()
        elif character == "," and not stack:
            parts.append(text[start:index].strip())
            start = index + 1
    if quote or stack:
        raise MappingError("unbalanced delimiters in CIRCT memory metadata")
    final = text[start:].strip()
    if final:
        parts.append(final)
    return parts


def decode_symbol(symbol: str) -> str:
    return json.loads(symbol) if symbol.startswith('"') else symbol


def parse_port(text: str) -> Port:
    match = _PORT_RE.match(text)
    if match is None:
        raise MappingError(f"cannot parse FIRRTLMem port: {text}")
    direction, name, mlir_type = match.groups()
    bit_type = _BIT_TYPE_RE.match(mlir_type)
    width = int(bit_type.group(1)) if bit_type is not None else None
    return Port(direction, name, mlir_type, width)


def parse_attributes(text: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    for item in split_top_level(text):
        if "=" not in item:
            raise MappingError(f"cannot parse FIRRTLMem attribute: {item}")
        name, value = item.split("=", 1)
        result[name.strip()] = value.strip()
    return result


def integer_attribute(attributes: Dict[str, str], name: str) -> int:
    if name not in attributes:
        raise MappingError(f"FIRRTLMem is missing the {name} attribute")
    match = _INTEGER_RE.match(attributes[name])
    if match is None:
        raise MappingError(f"FIRRTLMem {name} is not an integer: {attributes[name]}")
    return int(match.group(1))


def boolean_attribute(attributes: Dict[str, str], name: str) -> bool:
    if attributes.get(name) == "true":
        return True
    if attributes.get(name) == "false":
        return False
    raise MappingError(f"FIRRTLMem {name} is not a boolean: {attributes.get(name)}")


def string_attribute(attributes: Dict[str, str], name: str) -> str:
    try:
        value = json.loads(attributes[name])
    except (KeyError, json.JSONDecodeError) as error:
        raise MappingError(f"FIRRTLMem {name} is not a string") from error
    if not isinstance(value, str):
        raise MappingError(f"FIRRTLMem {name} is not a string")
    return value


def parse_memories(mlir: str, selected_only: bool = False) -> List[Memory]:
    generated_memories: List[Memory] = []
    selected_memories: List[Memory] = []
    for line_number, line in enumerate(mlir.splitlines(), 1):
        selected = "hw.module.extern" in line and "rhodium.memory.site" in line
        generated = "hw.module.generated" in line and "@FIRRTLMem" in line
        if selected_only and not selected:
            continue
        if not selected and not generated:
            continue
        match = (_SELECTED_EXTERN_RE if selected else _GENERATED_RE).match(line)
        if match is None:
            raise MappingError(
                f"line {line_number}: memories must use the pinned one-line CIRCT form"
            )
        ports = tuple(parse_port(item) for item in split_top_level(match.group("ports")))
        attributes = parse_attributes(match.group("attributes"))
        site = string_attribute(attributes, "rhodium.memory.site") if selected else None
        requested_macro = string_attribute(attributes, "rhodium.memory.macro") if selected else None
        source_module = string_attribute(attributes, "rhodium.memory.source") if selected else None
        memory = Memory(
            module=decode_symbol(match.group("module")),
            ports=ports,
            attributes=attributes,
            depth=integer_attribute(attributes, "depth"),
            width=integer_attribute(attributes, "width"),
            mask_granularity=integer_attribute(attributes, "maskGran"),
            site=site,
            requested_macro=requested_macro,
            source_module=source_module,
        )
        (selected_memories if selected else generated_memories).append(memory)
    memories = selected_memories if selected_only or selected_memories else generated_memories
    modules = [memory.module for memory in memories]
    if len(modules) != len(set(modules)):
        raise MappingError("the CIRCT input contains duplicate FIRRTLMem module names")
    sites = [memory.site for memory in memories if memory.site is not None]
    if len(sites) != len(set(sites)):
        raise MappingError("the CIRCT input contains duplicate selected memory sites")
    return memories


def require_port(
    memory: Memory,
    name: str,
    direction: str,
    width: Optional[int],
    mlir_type: Optional[str] = None,
) -> None:
    port = memory.port_map.get(name)
    if port is None:
        raise MappingError(f"{memory.module}: missing {name} port")
    if port.direction != direction or port.width != width:
        raise MappingError(f"{memory.module}: unexpected {name} port contract")
    if mlir_type is not None and port.mlir_type != mlir_type:
        raise MappingError(f"{memory.module}: unexpected {name} type {port.mlir_type}")


def validate_memory(memory: Memory) -> None:
    if integer_attribute(memory.attributes, "numReadPorts") != 0:
        raise MappingError(f"{memory.module}: separate read ports are not supported")
    if integer_attribute(memory.attributes, "numWritePorts") != 0:
        raise MappingError(f"{memory.module}: separate write ports are not supported")
    if integer_attribute(memory.attributes, "numReadWritePorts") != 1:
        raise MappingError(f"{memory.module}: exactly one read-write port is required")
    if integer_attribute(memory.attributes, "readLatency") != 1:
        raise MappingError(f"{memory.module}: read latency must be one")
    if integer_attribute(memory.attributes, "writeLatency") != 1:
        raise MappingError(f"{memory.module}: write latency must be one")
    if string_attribute(memory.attributes, "initFilename"):
        raise MappingError(f"{memory.module}: initialized SRAMs are not supported")
    if boolean_attribute(memory.attributes, "initIsInline"):
        raise MappingError(f"{memory.module}: inline SRAM initialization is not supported")
    boolean_attribute(memory.attributes, "initIsBinary")
    if memory.attributes.get("writeClockIDs") != "[0 : i32]":
        raise MappingError(f"{memory.module}: only the single default write clock is supported")
    if memory.depth <= 0 or memory.width <= 0 or memory.mask_granularity <= 0:
        raise MappingError(f"{memory.module}: depth, width, and mask granularity must be positive")
    address_width = max(1, (memory.depth - 1).bit_length())
    require_port(memory, "RW0_addr", "in", address_width)
    require_port(memory, "RW0_en", "in", 1)
    require_port(memory, "RW0_clk", "in", None, "!seq.clock")
    require_port(memory, "RW0_wmode", "in", 1)
    require_port(memory, "RW0_wdata", "in", memory.width)
    require_port(memory, "RW0_rdata", "out", memory.width)
    expected_names = {"RW0_addr", "RW0_en", "RW0_clk", "RW0_wmode", "RW0_wdata", "RW0_rdata"}
    if memory.mask_granularity < memory.width:
        if memory.width % memory.mask_granularity != 0:
            raise MappingError(f"{memory.module}: ragged logical write masks are not supported")
        require_port(memory, "RW0_wmask", "in", memory.width // memory.mask_granularity)
        expected_names.add("RW0_wmask")
    elif "RW0_wmask" in memory.port_map:
        raise MappingError(f"{memory.module}: unexpected write-mask port")
    if set(memory.port_map) != expected_names:
        extras = sorted(set(memory.port_map) - expected_names)
        raise MappingError(f"{memory.module}: unsupported ports: {', '.join(extras)}")


def load_catalog(path: Path) -> List[Macro]:
    parser = configparser.ConfigParser(interpolation=None)
    try:
        with path.open(encoding="utf-8") as catalog_file:
            parser.read_file(catalog_file)
    except (OSError, configparser.Error) as error:
        raise MappingError(f"cannot read macro catalog {path}: {error}") from error
    macros: List[Macro] = []
    for section in parser.sections():
        if not section.startswith("macro:"):
            continue
        values = parser[section]
        try:
            collateral = {
                kind: values[kind]
                for kind in ("verilog", "lef", "gds", "liberty", "spice")
            }
            macro = Macro(
                name=values["name"],
                interface=values["interface"],
                depth=values.getint("depth"),
                width=values.getint("width"),
                write_granularity=values.getint("write_granularity"),
                address_width=values.getint("address_width"),
                read_ports=values.getint("read_ports"),
                read_write_ports=values.getint("read_write_ports"),
                width_um=values.getfloat("width_um"),
                height_um=values.getfloat("height_um"),
                collateral=collateral,
                power_pins=tuple(
                    pin.strip()
                    for pin in values.get("power_pins", "").split(",")
                    if pin.strip()
                ),
                functional_verilog=(
                    (path.parent / values["functional_verilog"]).resolve()
                    if values.get("functional_verilog", "").strip()
                    else None
                ),
            )
        except (KeyError, ValueError, configparser.Error) as error:
            raise MappingError(f"invalid macro catalog section [{section}]: {error}") from error
        if macro.depth != 1 << macro.address_width:
            raise MappingError(f"{macro.name}: macro depth must match its address width")
        if macro.width <= 0 or macro.write_granularity <= 0 or macro.width % macro.write_granularity:
            raise MappingError(f"{macro.name}: macro width must be divisible by write granularity")
        if macro.interface != "openram_1rw1r":
            raise MappingError(
                f"{macro.name}: unsupported macro interface {macro.interface}"
            )
        if macro.read_ports != 1 or macro.read_write_ports != 1:
            raise MappingError(
                f"{macro.name}: openram_1rw1r requires one read and one read-write port"
            )
        if (
            macro.functional_verilog is not None
            and not macro.functional_verilog.is_file()
        ):
            raise MappingError(
                f"{macro.name}: functional Verilog model does not exist: "
                f"{macro.functional_verilog}"
            )
        macros.append(macro)
    if not macros:
        raise MappingError(f"macro catalog {path} contains no [macro:*] sections")
    names = [macro.name for macro in macros]
    if len(names) != len(set(names)):
        raise MappingError(f"macro catalog {path} contains duplicate macro names")
    return macros


def macro_is_compatible(memory: Memory, macro: Macro) -> bool:
    if macro.read_write_ports < 1:
        return False
    if memory.mask_width is None:
        return True
    return (
        macro.write_granularity <= memory.mask_granularity
        and memory.mask_granularity % macro.write_granularity == 0
    )


def plan_memory(memory: Memory, macros: Sequence[Macro]) -> Plan:
    validate_memory(memory)
    if memory.requested_macro is not None:
        requested = [macro for macro in macros if macro.name == memory.requested_macro]
        if not requested:
            raise MappingError(
                f"{memory.module}: policy requests unknown macro {memory.requested_macro}"
            )
        macros = requested
    candidates: List[Tuple[Tuple[float, int, int], Plan]] = []
    logical_bits = memory.depth * memory.width
    for macro in macros:
        if not macro_is_compatible(memory, macro):
            continue
        plan = Plan(
            memory=memory,
            macro=macro,
            depth_banks=math.ceil(memory.depth / macro.depth),
            width_slices=math.ceil(memory.width / macro.width),
        )
        allocated_bits = plan.instance_count * macro.depth * macro.width
        score = (plan.instance_count * macro.area_um2, allocated_bits - logical_bits, plan.instance_count)
        candidates.append((score, plan))
    if not candidates:
        if memory.requested_macro is not None:
            raise MappingError(
                f"{memory.module}: requested macro {memory.requested_macro} "
                "cannot preserve its write-mask contract"
            )
        raise MappingError(f"{memory.module}: no catalog macro can preserve its write-mask contract")
    candidates.sort(key=lambda candidate: candidate[0])
    return candidates[0][1]


def verify_collateral(plans: Sequence[Plan], pdk_root: Optional[Path]) -> None:
    if pdk_root is None:
        return
    missing = []
    macros = {plan.macro.name: plan.macro for plan in plans}
    for macro in macros.values():
        for kind, relative_path in macro.collateral.items():
            if not (pdk_root / relative_path).is_file():
                missing.append(f"{macro.name} {kind}: {pdk_root / relative_path}")
    if missing:
        raise MappingError("missing macro collateral:\n  " + "\n  ".join(sorted(missing)))


def load_site_inventory(path: Path) -> Dict[str, object]:
    try:
        inventory = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise MappingError(f"cannot read site inventory {path}: {error}") from error
    if not isinstance(inventory, dict) or inventory.get("schema_version") != 1:
        raise MappingError(f"site inventory {path} must use schema_version 1")
    if not isinstance(inventory.get("top"), str) or not inventory["top"]:
        raise MappingError(f"site inventory {path} has no top module")
    policy_top = inventory.get("policy_top", inventory["top"])
    if not isinstance(policy_top, str) or not policy_top:
        raise MappingError(f"site inventory {path} has no logical policy top")
    scope_prefix = inventory.get("scope_prefix", "")
    if not isinstance(scope_prefix, str):
        raise MappingError(f"site inventory {path} has an invalid scope prefix")
    inventory["policy_top"] = policy_top
    inventory["scope_prefix"] = scope_prefix
    if inventory.get("default") != "infer":
        raise MappingError(f"site inventory {path} must use the infer default")
    sites = inventory.get("sites")
    if not isinstance(sites, list):
        raise MappingError(f"site inventory {path} has no sites list")
    seen = set()
    for site in sites:
        if not isinstance(site, dict):
            raise MappingError(f"site inventory {path} contains a non-object site")
        site_path = site.get("path")
        if not isinstance(site_path, str) or not site_path:
            raise MappingError(f"site inventory {path} contains an invalid site path")
        if site_path in seen:
            raise MappingError(f"site inventory {path} repeats site {site_path}")
        seen.add(site_path)
        instance_path = site.get("instance_path", site_path)
        if not isinstance(instance_path, str) or not instance_path:
            raise MappingError(
                f"site inventory {path} has an invalid instance path for {site_path}"
            )
        site["instance_path"] = instance_path
        if not isinstance(site.get("source_module"), str) or not site["source_module"]:
            raise MappingError(f"site inventory {path} has no source module for {site_path}")
        decision = site.get("decision")
        if decision == "macro":
            if not isinstance(site.get("macro"), str) or not site["macro"]:
                raise MappingError(f"site inventory {path} has no macro for {site_path}")
            if not isinstance(site.get("wrapper_module"), str) or not site["wrapper_module"]:
                raise MappingError(f"site inventory {path} has no wrapper module for {site_path}")
        elif decision != "infer":
            raise MappingError(f"site inventory {path} has invalid decision for {site_path}")
    return inventory


def validate_site_inventory(memories: Sequence[Memory], inventory: Dict[str, object]) -> None:
    inventory_sites = inventory["sites"]
    assert isinstance(inventory_sites, list)
    expected = {
        site["path"]: site
        for site in inventory_sites
        if isinstance(site, dict) and site.get("decision") == "macro"
    }
    actual = {memory.site: memory for memory in memories}
    if None in actual:
        raise MappingError("site-selected mapping input contains an untagged memory")
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    if missing or extra:
        details = []
        if missing:
            details.append("missing selected sites: " + ", ".join(missing))
        if extra:
            details.append("unexpected selected sites: " + ", ".join(extra))
        raise MappingError("site inventory and selected MLIR disagree: " + "; ".join(details))
    for site_path, site in expected.items():
        memory = actual[site_path]
        if memory.module != site["wrapper_module"]:
            raise MappingError(f"{site_path}: selected wrapper does not match site inventory")
        if memory.requested_macro != site["macro"]:
            raise MappingError(f"{site_path}: requested macro does not match site inventory")
        if memory.source_module != site["source_module"]:
            raise MappingError(f"{site_path}: source module does not match site inventory")


def sv_range(width: int) -> str:
    return "" if width == 1 else f"[{width - 1}:0] "


def data_slice(signal: str, offset: int, width: int) -> str:
    if width == 1:
        return f"{signal}[{offset}]"
    return f"{signal}[{offset + width - 1}:{offset}]"


def render_openram_1rw1r_wrapper(plan: Plan) -> str:
    memory = plan.memory
    macro = plan.macro
    address_width = memory.port_map["RW0_addr"].width
    assert address_width is not None
    lines = [
        f"// Maps CIRCT memory {memory.site or memory.module} "
        f"({memory.depth} x {memory.width}) onto {plan.instance_count} SRAM macro(s).",
        f"module {memory.module}(",
    ]
    for index, port in enumerate(memory.ports):
        if port.width is None and port.mlir_type != "!seq.clock":
            raise MappingError(f"{memory.module}: cannot render port type {port.mlir_type}")
        width = 1 if port.width is None else port.width
        comma = "," if index + 1 < len(memory.ports) else ""
        lines.append(f"  {port.direction}put wire {sv_range(width)}{port.name}{comma}")
    lines.extend([");", ""])
    if address_width < macro.address_width:
        padding = macro.address_width - address_width
        address_expression = f"{{{{{padding}{{1'b0}}}}, RW0_addr}}"
    elif address_width == macro.address_width:
        address_expression = "RW0_addr"
    else:
        address_expression = f"RW0_addr[{macro.address_width - 1}:0]"
    lines.append(f"  wire [{macro.address_width - 1}:0] macro_addr = {address_expression};")
    if plan.depth_banks > 1:
        bank_width = max(1, (plan.depth_banks - 1).bit_length())
        lines.extend(
            [
                f"  wire [{bank_width - 1}:0] current_bank = RW0_addr[{address_width - 1}:{macro.address_width}];",
                f"  reg [{bank_width - 1}:0] read_bank;",
                "  always @(posedge RW0_clk) begin",
                "    if (RW0_en && !RW0_wmode)",
                "      read_bank <= current_bank;",
                "  end",
            ]
        )
    for bank in range(plan.depth_banks):
        if plan.depth_banks == 1:
            selection = "1'b1"
        else:
            bank_width = max(1, (plan.depth_banks - 1).bit_length())
            selection = f"current_bank == {bank_width}'d{bank}"
        lines.append(f"  wire bank_{bank}_selected = {selection};")
        lines.append(f"  wire bank_{bank}_csb0 = ~(RW0_en && bank_{bank}_selected);")
    lines.append("")
    for width_slice in range(plan.width_slices):
        offset = width_slice * macro.width
        used_width = min(macro.width, memory.width - offset)
        if used_width == macro.width:
            input_expression = data_slice("RW0_wdata", offset, used_width)
        else:
            padding = macro.width - used_width
            input_expression = f"{{{{{padding}{{1'b0}}}}, {data_slice('RW0_wdata', offset, used_width)}}}"
        mask_bits = []
        for lane in range(macro.mask_width):
            logical_bit = offset + lane * macro.write_granularity
            if logical_bit >= memory.width:
                mask_bits.append("1'b0")
            elif memory.mask_width is None:
                mask_bits.append("1'b1")
            else:
                mask_bits.append(f"RW0_wmask[{logical_bit // memory.mask_granularity}]")
        lines.append(f"  wire [{macro.width - 1}:0] macro_din_w{width_slice} = {input_expression};")
        lines.append(
            f"  wire [{macro.mask_width - 1}:0] macro_wmask_w{width_slice} = "
            "{" + ", ".join(reversed(mask_bits)) + "};"
        )
    lines.append("")
    for bank in range(plan.depth_banks):
        for width_slice in range(plan.width_slices):
            output_name = f"macro_dout_d{bank}_w{width_slice}"
            lines.extend(
                [
                    f"  wire [{macro.width - 1}:0] {output_name};",
                    f"  {macro.name} mem_d{bank}_w{width_slice} (",
                    "    .clk0(RW0_clk),",
                    f"    .csb0(bank_{bank}_csb0),",
                    "    .web0(~RW0_wmode),",
                    f"    .wmask0(macro_wmask_w{width_slice}),",
                    "    .addr0(macro_addr),",
                    f"    .din0(macro_din_w{width_slice}),",
                    f"    .dout0({output_name}),",
                    "    .clk1(RW0_clk),",
                    "    .csb1(1'b1),",
                    f"    .addr1({macro.address_width}'b0),",
                    "    .dout1()",
                    "  );",
                ]
            )
    lines.append("")
    for bank in range(plan.depth_banks):
        pieces = []
        for width_slice in reversed(range(plan.width_slices)):
            offset = width_slice * macro.width
            used_width = min(macro.width, memory.width - offset)
            signal = f"macro_dout_d{bank}_w{width_slice}"
            pieces.append(signal if used_width == macro.width else data_slice(signal, 0, used_width))
        expression = pieces[0] if len(pieces) == 1 else "{" + ", ".join(pieces) + "}"
        lines.append(f"  wire {sv_range(memory.width)}bank_{bank}_rdata = {expression};")
    if plan.depth_banks == 1:
        lines.append("  assign RW0_rdata = bank_0_rdata;")
    else:
        lines.extend(
            [
                f"  reg {sv_range(memory.width)}selected_rdata;",
                "  always @* begin",
                "    case (read_bank)",
            ]
        )
        for bank in range(plan.depth_banks):
            lines.append(f"      {bank}: selected_rdata = bank_{bank}_rdata;")
        lines.extend(
            [
                f"      default: selected_rdata = {{{memory.width}{{1'bx}}}};",
                "    endcase",
                "  end",
                "  assign RW0_rdata = selected_rdata;",
            ]
        )
    lines.extend(["endmodule", ""])
    return "\n".join(lines)


def render_wrapper(plan: Plan) -> str:
    if plan.macro.interface == "openram_1rw1r":
        return render_openram_1rw1r_wrapper(plan)
    raise MappingError(
        f"{plan.macro.name}: no wrapper renderer for interface {plan.macro.interface}"
    )


def render_verilog(plans: Sequence[Plan], source: Path) -> str:
    lines = [
        "// Implements selected CIRCT memory sites using catalogued physical SRAM macros.",
        f"// Generated from {source}; edit the mapper or catalog, not this file.",
        "",
    ]
    for plan in plans:
        lines.append(render_wrapper(plan))
    return "\n".join(lines)


def plan_manifest(
    plans: Sequence[Plan],
    source: Path,
    catalog: Path,
    pdk_root: Optional[Path],
    site_inventory: Optional[Dict[str, object]] = None,
    site_inventory_path: Optional[Path] = None,
) -> Dict[str, object]:
    memories = []
    total_instances = 0
    total_area = 0.0
    for plan in plans:
        memory = plan.memory
        macro = plan.macro
        allocated_bits = plan.instance_count * macro.depth * macro.width
        logical_bits = memory.depth * memory.width
        area = plan.instance_count * macro.area_um2
        total_instances += plan.instance_count
        total_area += area
        instances = [
            {"name": f"mem_d{bank}_w{width_slice}", "depth_bank": bank, "width_slice": width_slice}
            for bank in range(plan.depth_banks)
            for width_slice in range(plan.width_slices)
        ]
        memory_entry = {
            "module": memory.module,
            "logical": {
                "depth": memory.depth,
                "width": memory.width,
                "mask_granularity": memory.mask_granularity,
                "mask_width": memory.mask_width,
                "read_ports": 0,
                "write_ports": 0,
                "read_write_ports": 1,
                "read_latency": 1,
                "write_latency": 1,
            },
            "mapping": {
                "macro": macro.name,
                "interface": macro.interface,
                "depth_banks": plan.depth_banks,
                "width_slices": plan.width_slices,
                "instances": instances,
                "logical_bits": logical_bits,
                "allocated_bits": allocated_bits,
                "bit_utilization": logical_bits / allocated_bits,
                "macro_area_um2": area,
            },
            "collateral": macro.collateral,
            "power_pins": list(macro.power_pins),
            "functional_verilog": (
                None
                if macro.functional_verilog is None
                else str(macro.functional_verilog)
            ),
        }
        if memory.site is None:
            memory_entry["mapping"]["instances_per_definition"] = plan.instance_count
        else:
            memory_entry["site"] = memory.site
            memory_entry["source_module"] = memory.source_module
            memory_entry["mapping"]["instances_per_site"] = plan.instance_count
        memories.append(memory_entry)
    manifest = {
        "schema_version": 1,
        "source_mlir": str(source),
        "catalog": str(catalog),
        "pdk_root": None if pdk_root is None else str(pdk_root),
        "selection_policy": (
            "lowest total catalogued macro area preserving the one-RW-port "
            "and write-mask contract"
        ),
        "scope": (
            "unique FIRRTLMem module definitions; hierarchical occurrence "
            "counts require a later deuniquification pass"
        ),
        "memories": memories,
        "totals_per_unique_definition": {
            "memory_definitions": len(memories),
            "macro_instances": total_instances,
            "macro_area_um2": total_area,
            "macro_area_mm2": total_area / 1_000_000.0,
        },
        "physical_handoff": {
            "requires": [
                "macro LEF, GDS, Liberty, Verilog, and SPICE views from each memory entry",
                "floorplan placement and row/blockage generation for every hierarchical macro occurrence",
                "PDN connections for each macro power pin",
                "macro-aware LVS setup using the listed SPICE view",
            ]
        },
    }
    if site_inventory is None:
        return manifest

    inventory_sites = site_inventory["sites"]
    assert isinstance(inventory_sites, list)
    mapped_by_site = {memory["site"]: memory for memory in memories}
    sites = []
    for inventory_site in inventory_sites:
        assert isinstance(inventory_site, dict)
        site = dict(inventory_site)
        if site["decision"] == "macro":
            mapped = mapped_by_site[site["path"]]
            site["logical"] = mapped["logical"]
            site["mapping"] = mapped["mapping"]
            site["collateral"] = mapped["collateral"]
            site["power_pins"] = mapped["power_pins"]
            site["functional_verilog"] = mapped["functional_verilog"]
        sites.append(site)
    mapped_sites = len(memories)
    manifest.update(
        {
            "schema_version": 2,
            "site_inventory": (
                None if site_inventory_path is None else str(site_inventory_path)
            ),
            "selection_policy": {
                "default": site_inventory["default"],
                "top": site_inventory["top"],
                "policy_top": site_inventory["policy_top"],
                "scope_prefix": site_inventory["scope_prefix"],
            },
            "scope": (
                f"{site_inventory['policy_top']}-relative logical memory occurrences "
                f"selected from {site_inventory['top']}"
                + (
                    f" under {site_inventory['scope_prefix']}"
                    if site_inventory["scope_prefix"]
                    else ""
                )
            ),
            "sites": sites,
            "totals": {
                "memory_sites": len(inventory_sites),
                "mapped_sites": mapped_sites,
                "inferred_sites": len(inventory_sites) - mapped_sites,
                "macro_instances": total_instances,
                "macro_area_um2": total_area,
                "macro_area_mm2": total_area / 1_000_000.0,
            },
        }
    )
    manifest.pop("totals_per_unique_definition")
    return manifest


def write_output(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_mlir", type=Path, help="MLIR after circt-opt --lower-seq-firmem")
    parser.add_argument("--catalog", required=True, type=Path, help="INI macro catalog")
    parser.add_argument("--output-verilog", required=True, type=Path, help="generated SRAM wrapper file")
    parser.add_argument(
        "--output-manifest",
        required=True,
        type=Path,
        help="generated physical handoff manifest",
    )
    parser.add_argument("--pdk-root", type=Path, help="optional root used to verify catalogued collateral")
    parser.add_argument(
        "--site-inventory",
        type=Path,
        help="occurrence inventory emitted by the CIRCT site pass",
    )
    arguments = parser.parse_args(argv)
    try:
        site_inventory = (
            load_site_inventory(arguments.site_inventory)
            if arguments.site_inventory is not None
            else None
        )
        memories = parse_memories(
            arguments.input_mlir.read_text(encoding="utf-8"),
            selected_only=site_inventory is not None,
        )
        if site_inventory is not None:
            validate_site_inventory(memories, site_inventory)
        macros = load_catalog(arguments.catalog)
        plans = [plan_memory(memory, macros) for memory in memories]
        verify_collateral(plans, arguments.pdk_root)
        write_output(arguments.output_verilog, render_verilog(plans, arguments.input_mlir))
        manifest = plan_manifest(
            plans,
            arguments.input_mlir,
            arguments.catalog,
            arguments.pdk_root,
            site_inventory,
            arguments.site_inventory,
        )
        write_output(arguments.output_manifest, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    except (OSError, MappingError) as error:
        print(f"memory mapping failed: {error}", file=sys.stderr)
        return 2
    if site_inventory is None:
        totals = manifest["totals_per_unique_definition"]
        print(
            f"mapped {totals['memory_definitions']} FIRRTLMem definitions to "
            f"{totals['macro_instances']} macro instances ({totals['macro_area_mm2']:.3f} mm^2 per unique definition set)"
        )
    else:
        totals = manifest["totals"]
        print(
            f"mapped {totals['mapped_sites']} of {totals['memory_sites']} memory sites to "
            f"{totals['macro_instances']} macro instances ({totals['macro_area_mm2']:.3f} mm^2)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())

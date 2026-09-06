<!-- Documents the generic host-side device-tree model and its DTS and DTB encoders. -->

# Device trees

`devicetree` is a dependency-neutral Rhombus package for constructing validated
device trees and producing both inspection-oriented DTS text and native flattened
device-tree binaries. It does not depend on Rhodium, RISC-V, CHI, or an external
device-tree compiler.

The public API is exported by [`main.rhm`](main.rhm). A tree is composed from
`DeviceTreeNode` and `DeviceTreeProperty` values. Property data is represented by
explicit types:

- `DeviceTreeU32` and `DeviceTreeU64` for unsigned fixed-width values
- `DeviceTreeCells` for heterogeneous cell sequences
- `DeviceTreeString` and `DeviceTreeStringList` for NUL-terminated UTF-8 data
- `DeviceTreeEmpty` for presence-only properties
- `DeviceTreePhandle` for a reference to a labeled node

`DeviceTreeLabel` values connect nodes and references. Every labeled node receives
a phandle in deterministic preorder, beginning at one. Both `to_dts()` and
`to_dtb()` use that assignment. The encoder owns synthesized `phandle` properties;
callers cannot provide `phandle` or `linux,phandle` manually.

```rhombus
#lang rhombus

import:
  lib("devicetree/main.rhm") open

def intc = DeviceTreeLabel("cpu_intc_0")
def tree = DeviceTree(
  DeviceTreeNode(
    "",
    [DeviceTreeProperty("compatible", DeviceTreeString("example,soc"))],
    [DeviceTreeNode(
       "interrupt-controller",
       [DeviceTreeProperty("interrupt-controller", DeviceTreeEmpty())],
       [],
       ~label: intc),
     DeviceTreeNode(
       "consumer",
       [DeviceTreeProperty("interrupt-parent", DeviceTreePhandle(intc))])]
  )
)

def dts = tree.to_dts()
def dtb = tree.to_dtb()
```

The DTB encoder emits version-17 flattened trees with a complete header, 64-bit
memory reservation map, aligned structure block, deduplicated property-name
string block, and big-endian cells. Construction rejects invalid node or property
names, duplicate properties and children, duplicate or unresolved labels, NULs in
strings, out-of-width integers, and invalid reservations.

Run the package tests with:

```sh
make devicetree-test
```

That target runs host-model tests and cross-checks a representative native DTB
with `dtc`, `fdtdump`, and `fdtget`. These tools are test oracles only and are not
required by package users.

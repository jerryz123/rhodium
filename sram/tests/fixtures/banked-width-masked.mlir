// Exercises generic depth banking, width slicing, a partial final slice, and logical byte masks.
// SPDX-License-Identifier: Apache-2.0
module {
  hw.generator.schema @FIRRTLMem, "FIRRTL_Memory", ["depth", "numReadPorts", "numWritePorts", "numReadWritePorts", "readLatency", "writeLatency", "width", "maskGran", "readUnderWrite", "writeUnderWrite", "writeClockIDs", "initFilename", "initIsBinary", "initIsInline"]
  hw.module.generated @storage_1024x40, @FIRRTLMem(in %RW0_addr : i10, in %RW0_en : i1, in %RW0_clk : !seq.clock, in %RW0_wmode : i1, in %RW0_wdata : i40, out RW0_rdata : i40, in %RW0_wmask : i5) attributes {depth = 1024 : i64, initFilename = "", initIsBinary = false, initIsInline = false, maskGran = 8 : ui32, numReadPorts = 0 : ui32, numReadWritePorts = 1 : ui32, numWritePorts = 0 : ui32, readLatency = 1 : ui32, readUnderWrite = 0 : i32, width = 40 : ui32, writeClockIDs = [0 : i32], writeLatency = 1 : ui32, writeUnderWrite = 0 : i32}
}

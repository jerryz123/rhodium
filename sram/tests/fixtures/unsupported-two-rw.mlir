// Exercises rejection of a generated memory whose port topology is outside the generic mapper contract.
// SPDX-License-Identifier: Apache-2.0
module {
  hw.generator.schema @FIRRTLMem, "FIRRTL_Memory", ["depth", "numReadPorts", "numWritePorts", "numReadWritePorts", "readLatency", "writeLatency", "width", "maskGran", "readUnderWrite", "writeUnderWrite", "writeClockIDs", "initFilename", "initIsBinary", "initIsInline"]
  hw.module.generated @storage_512x32_2rw, @FIRRTLMem(in %RW0_addr : i9, in %RW0_en : i1, in %RW0_clk : !seq.clock, in %RW0_wmode : i1, in %RW0_wdata : i32, out RW0_rdata : i32) attributes {depth = 512 : i64, initFilename = "", initIsBinary = false, initIsInline = false, maskGran = 32 : ui32, numReadPorts = 0 : ui32, numReadWritePorts = 2 : ui32, numWritePorts = 0 : ui32, readLatency = 1 : ui32, readUnderWrite = 0 : i32, width = 32 : ui32, writeClockIDs = [0 : i32], writeLatency = 1 : ui32, writeUnderWrite = 0 : i32}
}

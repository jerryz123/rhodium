// Provides two equal-shaped memory occurrences beneath a nested leaf for scoped site-selection tests.
// SPDX-License-Identifier: Apache-2.0
module {
  hw.generator.schema @FIRRTLMem, "FIRRTL_Memory", ["depth", "numReadPorts", "numWritePorts", "numReadWritePorts", "readLatency", "writeLatency", "width", "maskGran", "readUnderWrite", "writeUnderWrite", "writeClockIDs", "initFilename", "initIsBinary", "initIsInline"]
  hw.module.generated @storage_64x52, @FIRRTLMem(in %RW0_addr : i6, in %RW0_en : i1, in %RW0_clk : !seq.clock, in %RW0_wmode : i1, in %RW0_wdata : i52, out RW0_rdata : i52) attributes {depth = 64 : i64, initFilename = "", initIsBinary = false, initIsInline = false, maskGran = 52 : ui32, numReadPorts = 0 : ui32, numReadWritePorts = 1 : ui32, numWritePorts = 0 : ui32, readLatency = 1 : ui32, readUnderWrite = 0 : i32, width = 52 : ui32, writeClockIDs = [0 : i32], writeLatency = 1 : ui32, writeUnderWrite = 0 : i32}
  hw.module @Top(in %clock : i1, in %address : i6, in %enable : i1, in %write : i1, in %data : i52, out left : i52, out right : i52) {
    %left, %right = hw.instance "leaf" @Leaf(clock: %clock: i1, address: %address: i6, enable: %enable: i1, write: %write: i1, data: %data: i52) -> (left: i52, right: i52)
    hw.output %left, %right : i52, i52
  }
  hw.module @Leaf(in %clock : i1, in %address : i6, in %enable : i1, in %write : i1, in %data : i52, out left : i52, out right : i52) {
    %seq_clock = seq.to_clock %clock
    %left = hw.instance "left/storage_ext" @storage_64x52(RW0_addr: %address: i6, RW0_en: %enable: i1, RW0_clk: %seq_clock: !seq.clock, RW0_wmode: %write: i1, RW0_wdata: %data: i52) -> (RW0_rdata: i52)
    %right = hw.instance "right/storage_ext" @storage_64x52(RW0_addr: %address: i6, RW0_en: %enable: i1, RW0_clk: %seq_clock: !seq.clock, RW0_wmode: %write: i1, RW0_wdata: %data: i52) -> (RW0_rdata: i52)
    hw.output %left, %right : i52, i52
  }
}

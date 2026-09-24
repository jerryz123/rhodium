// Requires the instrumented stable-identity assertion to reject an active-epoch change.
// SPDX-License-Identifier: Apache-2.0
module event_instance_invalid_tb;
  event_instance_tb #(.BAD(1)) bench();
endmodule

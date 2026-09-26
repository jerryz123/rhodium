// Rebinds generated litmus7 stream arguments to inert bare-metal port objects.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_LITMUS7_STREAMS_H
#define RHODIUM_LITMUS7_STREAMS_H

#include <stdio.h>

extern FILE litmus_baremetal_stdout_stream;
extern FILE litmus_baremetal_stderr_stream;

#undef stdout
#undef stderr
#define stdout (&litmus_baremetal_stdout_stream)
#define stderr (&litmus_baremetal_stderr_stream)

#endif

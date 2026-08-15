#pragma once

namespace tinydraw::esp32 {

// CO5300 hardware characterization probe (Block A: software-only physics).
// Measures TE signal statistics, PSRAM staging bandwidth, windowed-push versus
// single-window continuation-stream throughput, TE-synchronized full-frame
// cadence, and GETSCANLINE readability. Prints TINYDRAW_PROBE_* serial
// receipts and never claims optical correctness: tear/notch verdicts belong to
// the Block B camera cells.
void run_panel_probe();

}  // namespace tinydraw::esp32

#pragma once

// Thin wrapper around ESP-IDF's SNTP client.
// Call SntpSync::start() once after Wi-Fi obtains an IP.
// ShelfbotTimestamp::isEpochValid() will return true within ~1–3 s.

namespace SntpSync {

// Start the SNTP client. Safe to call multiple times — no-ops if already running.
void start();

// Returns true if the POSIX clock has been set to a sane epoch (> Jan 2023).
// Equivalent to ShelfbotTimestamp::isEpochValid() but available without
// pulling in the full timestamp header.
bool isSynced();

} // namespace SntpSync

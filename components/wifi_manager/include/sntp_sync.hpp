#pragma once

// Thin wrapper around ESP-IDF's SNTP client.
// Call sntp_sync_start() once after Wi-Fi connects.
// isEpochValid() from ShelfbotTimestamp will return true within ~1-2 s.

namespace SntpSync {

// Start the SNTP client pointing at pool.ntp.org.
// Safe to call multiple times — no-ops if already running.
void start();

} // namespace SntpSync

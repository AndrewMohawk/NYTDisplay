#pragma once

constexpr char TIME_ZONE[] = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.cloudflare.com";
constexpr unsigned CHECK_INTERVAL_SECONDS = 3 * 3600;
constexpr unsigned RETRY_SECONDS = 3600;
constexpr unsigned WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr unsigned NTP_SYNC_TIMEOUT_MS = 20000;

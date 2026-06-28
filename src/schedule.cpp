#include "schedule.h"

WeeklySchedule defaultSchedule() {
  WeeklySchedule s;
  for (int d = 0; d < 7; d++) {
    s.days[d].enabled = true;
    s.days[d].onMin = 6 * 60;   // 06:00
    s.days[d].offMin = 0;       // 00:00 (wraps → on until midnight)
  }
  return s;
}

int timeStrToMinutes(const char* hhmm) {
  if (!hhmm) return 0;
  int h = 0, m = 0;
  if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return 0;
  if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
  return h * 60 + m;
}

String minutesToTimeStr(int minutes) {
  if (minutes < 0) minutes = 0;
  minutes %= (24 * 60);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return String(buf);
}

PowerMode parsePowerMode(const char* s) {
  if (s && strcmp(s, "on") == 0) return POWER_ON;
  if (s && strcmp(s, "off") == 0) return POWER_OFF;
  return POWER_AUTO;
}

const char* powerModeStr(PowerMode mode) {
  switch (mode) {
    case POWER_ON: return "on";
    case POWER_OFF: return "off";
    default: return "auto";
  }
}

bool isSignOn(const WeeklySchedule& schedule, const struct tm& when) {
  int minutesNow = when.tm_hour * 60 + when.tm_min;
  int today = when.tm_wday;  // 0..6

  // Check today's window, then yesterday's (for windows that wrap past midnight).
  for (int offset = 0; offset >= -1; offset--) {
    int day = (today + offset + 7) % 7;
    const DaySchedule& d = schedule.days[day];
    if (!d.enabled) continue;

    if (d.offMin > d.onMin) {
      // Same-day window; only relevant for today.
      if (offset == 0 && minutesNow >= d.onMin && minutesNow < d.offMin) return true;
    } else {
      // Wraps past midnight: [on, 24:00) today and [00:00, off) tomorrow.
      if (offset == 0 && minutesNow >= d.onMin) return true;
      if (offset == -1 && minutesNow < d.offMin) return true;
    }
  }
  return false;
}

bool effectiveSignOn(PowerMode mode, const WeeklySchedule& schedule, const struct tm& when) {
  if (mode == POWER_ON) return true;
  if (mode == POWER_OFF) return false;
  return isSignOn(schedule, when);
}

void scheduleToJson(const WeeklySchedule& schedule, JsonObject out) {
  for (int d = 0; d < 7; d++) {
    JsonObject day = out[String(d)].to<JsonObject>();
    day["enabled"] = schedule.days[d].enabled;
    day["on"] = minutesToTimeStr(schedule.days[d].onMin);
    day["off"] = minutesToTimeStr(schedule.days[d].offMin);
  }
}

void scheduleFromJson(JsonVariantConst in, WeeklySchedule& out) {
  out = defaultSchedule();
  if (!in.is<JsonObjectConst>()) return;
  JsonObjectConst obj = in.as<JsonObjectConst>();
  for (int d = 0; d < 7; d++) {
    JsonVariantConst day = obj[String(d)];
    if (!day.is<JsonObjectConst>()) continue;
    out.days[d].enabled = day["enabled"] | true;
    if (day["on"].is<const char*>()) out.days[d].onMin = timeStrToMinutes(day["on"]);
    if (day["off"].is<const char*>()) out.days[d].offMin = timeStrToMinutes(day["off"]);
  }
}

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

// Per-day on/off window. Times are minutes since local midnight.
// If offMin <= onMin the window wraps past midnight (e.g. on 22:00 / off 06:00).
struct DaySchedule {
  bool enabled;
  int onMin;
  int offMin;
};

// Indexed 0 (Sunday) .. 6 (Saturday) to match struct tm.tm_wday.
struct WeeklySchedule {
  DaySchedule days[7];
};

// Manual power override for the sign.
enum PowerMode { POWER_AUTO, POWER_ON, POWER_OFF };

// Default: on every day from 06:00 to midnight.
WeeklySchedule defaultSchedule();

// "HH:MM" <-> minutes-since-midnight helpers.
int timeStrToMinutes(const char* hhmm);
String minutesToTimeStr(int minutes);

PowerMode parsePowerMode(const char* s);
const char* powerModeStr(PowerMode mode);

// Does the schedule say the sign should be on at the given local time?
bool isSignOn(const WeeklySchedule& schedule, const struct tm& when);

// Effective state combining the manual override with the schedule.
bool effectiveSignOn(PowerMode mode, const WeeklySchedule& schedule, const struct tm& when);

// JSON (de)serialization, keyed "0".."6" with { enabled, on, off }.
void scheduleToJson(const WeeklySchedule& schedule, JsonObject out);
void scheduleFromJson(JsonVariantConst in, WeeklySchedule& out);

#endif

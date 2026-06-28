#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "display.h"

static const int TITLE_BUF_LEN = 32;

struct ParsedTrain {
    char route;
    char direction;
    std::string stationName;
    int minutesAway;
};

inline void processTrains(std::vector<ParsedTrain>& trains,
                          TrainArrival* arrivals,
                          char titleBuffers[][TITLE_BUF_LEN],
                          int& arrivalCount) {
    // J southbound at Broad St is the last stop — nothing to board
    trains.erase(std::remove_if(trains.begin(), trains.end(),
        [](const ParsedTrain& t) { return t.route == 'J' && t.direction == 'S'; }),
        trains.end());

    std::sort(trains.begin(), trains.end(),
        [](const ParsedTrain& a, const ParsedTrain& b) { return a.minutesAway < b.minutesAway; });

    arrivalCount = 0;
    for (auto& t : trains) {
        const char* dirStr = t.direction == 'N' ? "N" : "S";

        // Try to pair with an existing entry for the same route+station+direction
        bool grouped = false;
        for (int i = 0; i < arrivalCount; i++) {
            if (arrivals[i].route != t.route || arrivals[i].minutesAway2 != -1) continue;
            if (arrivals[i].minutesAway == t.minutesAway) continue;  // skip duplicate/identical time
            char expected[TITLE_BUF_LEN];
            snprintf(expected, sizeof(expected), "%s %sB", t.stationName.c_str(), dirStr);
            if (strcmp(arrivals[i].title, expected) == 0) {
                arrivals[i].minutesAway2 = t.minutesAway;
                grouped = true;
                break;
            }
        }
        if (grouped) continue;
        if (arrivalCount >= MAX_TRAINS) continue;

        snprintf(titleBuffers[arrivalCount], TITLE_BUF_LEN, "%s %sB", t.stationName.c_str(), dirStr);
        arrivals[arrivalCount].route        = t.route;
        arrivals[arrivalCount].title        = titleBuffers[arrivalCount];
        arrivals[arrivalCount].minutesAway  = t.minutesAway;
        arrivals[arrivalCount].minutesAway2 = -1;
        arrivalCount++;
    }
}

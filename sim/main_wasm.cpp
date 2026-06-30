// Browser (WebAssembly) entry point for the subway sign simulator.
//
// Shares display.cpp + MatrixPanel_mock.cpp with the native sim, so the
// rendering is byte-for-byte the same as the firmware. The only differences
// from main_sim.cpp are the parts the browser does differently:
//   - No libcurl / no std::thread. The page fetches the API in JS and pushes
//     the raw JSON into WASM via feed_trains() (works around the API having
//     no CORS headers — the page can fetch through a same-origin proxy).
//   - The render loop is driven by emscripten_set_main_loop (requestAnimationFrame)
//     instead of an infinite while(true) + sleep.
#include "../src/train_parser.h"
#include <nlohmann/json.hpp>
#include <emscripten.h>
#include <emscripten/html5.h>

// Same buffers the native sim uses to back TrainArrival::title pointers.
static char titleBuffers[MAX_TRAINS][TITLE_BUF_LEN];
static TrainArrival arrivals[MAX_TRAINS];
static int arrivalCount = 0;

// Called by JS with the raw response body from /trains/nearby. Single-threaded
// in the browser, so no mutex is needed — we parse and apply inline.
extern "C" EMSCRIPTEN_KEEPALIVE void feed_trains(const char* json) {
    if (!json || !*json) return;
    try {
        auto doc = nlohmann::json::parse(json);

        std::vector<ParsedTrain> allTrains;
        for (auto& station : doc["stations"]) {
            std::string stationName = station["station_name"];
            for (const char* key : {"northbound_trains", "southbound_trains"}) {
                for (auto& train : station[key]) {
                    std::string route = train["route"];
                    std::string dir   = train["direction"];
                    allTrains.push_back({route[0], dir[0], stationName, train["minutes_away"]});
                }
            }
        }

        processTrains(allTrains, arrivals, titleBuffers, arrivalCount);
        setTrainArrivals(arrivals, arrivalCount);
    } catch (...) {
        // Malformed payload — keep showing whatever is on the panel.
    }
}

static void tick() {
    updateDisplay();
}

int main() {
    initDisplay();

    static TrainArrival loading[] = {
        {'?', "Loading...", 0, -1},
        {'?', "Loading...", 0, -1},
    };
    setTrainArrivals(loading, 2);

    // 0 fps => use requestAnimationFrame; 1 => keep the C++ runtime alive
    // after main() returns so feed_trains() stays callable.
    emscripten_set_main_loop(tick, 0, 1);
    return 0;
}

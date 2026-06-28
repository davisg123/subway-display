#include "../src/train_parser.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <mutex>

static const char* API_URL =
    "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com/trains/nearby"
    "?lat=40.706400&lon=-74.011400&limit=4";

static const int POLL_INTERVAL_SEC = 60;

static size_t curlWrite(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string fetchUrl(const char* url) {
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return response;
}

static char titleBuffers[4][TITLE_BUF_LEN];
static TrainArrival arrivals[4];
static int arrivalCount = 0;
// All SDL rendering must happen on the main thread; the fetch thread only
// parses into the buffers above and raises hasNewData under this mutex.
static std::mutex dataMutex;
static bool hasNewData = false;

static void fetchAndUpdate() {
    std::string json = fetchUrl(API_URL);
    if (json.empty()) return;

    try {
        auto doc = nlohmann::json::parse(json);

        std::vector<ParsedTrain> allTrains;

        for (auto& station : doc["stations"]) {
            std::string stationName = station["station_name"];

            for (auto& train : station["northbound_trains"]) {
                std::string route = train["route"];
                std::string dir   = train["direction"];
                allTrains.push_back({route[0], dir[0], stationName, train["minutes_away"]});
            }

            for (auto& train : station["southbound_trains"]) {
                std::string route = train["route"];
                std::string dir   = train["direction"];
                allTrains.push_back({route[0], dir[0], stationName, train["minutes_away"]});
            }
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        processTrains(allTrains, arrivals, titleBuffers, arrivalCount);
        hasNewData = true;  // main thread will apply + render
    } catch (...) {}
}

int main() {
    initDisplay();

    static TrainArrival loading[] = {
        {'?', "Loading...", 0, -1},
        {'?', "Loading...", 0, -1},
    };
    setTrainArrivals(loading, 2);

    std::thread([]() {
        while (true) {
            fetchAndUpdate();
            std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SEC));
        }
    }).detach();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (hasNewData) {
                setTrainArrivals(arrivals, arrivalCount);
                hasNewData = false;
            }
        }
        updateDisplay();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

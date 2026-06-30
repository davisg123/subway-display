# Subway Sign Simulator
# Requires SDL2: brew install sdl2
#
# Usage:
#   cd sim && make && ./subway_sim

CXX      := g++
CXXFLAGS := -std=c++17 -Imock_includes -I../src \
            -I/opt/homebrew/opt/nlohmann-json/include \
            $(shell sdl2-config --cflags) \
            -Wall -Wextra -Wno-unused-parameter
LDFLAGS  := $(shell sdl2-config --libs) -lcurl

SRCS     := main_sim.cpp MatrixPanel_mock.cpp ../src/display.cpp
TARGET   := subway_sim

# --- WebAssembly build (Emscripten) --------------------------------------
# Reuses display.cpp + MatrixPanel_mock.cpp (SDL2 is provided by emscripten);
# only the entry point differs (main_wasm.cpp instead of main_sim.cpp).
# Run `source ~/emsdk/emsdk_env.sh` first so emcc is on PATH.
EMCC      := emcc
WASM_SRCS := main_wasm.cpp MatrixPanel_mock.cpp ../src/display.cpp
WASM_OUT  := web/subway_sim.js
WASM_FLAGS := -std=c++17 -O2 -Imock_includes -I../src \
              -I/opt/homebrew/opt/nlohmann-json/include \
              -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 \
              -sEXPORTED_FUNCTIONS=_main,_feed_trains \
              -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
              -Wall -Wextra -Wno-unused-parameter

.PHONY: all clean run wasm wasm-serve

all: $(TARGET)

$(TARGET): $(SRCS) mock_includes/ESP32-HUB75-MatrixPanel-I2S-DMA.h ../src/display.h
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

run: all
	./$(TARGET)

wasm: $(WASM_OUT)

$(WASM_OUT): $(WASM_SRCS) mock_includes/ESP32-HUB75-MatrixPanel-I2S-DMA.h ../src/display.h main_wasm.cpp
	$(EMCC) $(WASM_FLAGS) $(WASM_SRCS) -o $(WASM_OUT)

# Serve the page + a CORS proxy to the live API for local testing.
wasm-serve: wasm
	cd web && python3 serve.py

clean:
	rm -f $(TARGET) web/subway_sim.js web/subway_sim.wasm

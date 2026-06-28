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

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) mock_includes/ESP32-HUB75-MatrixPanel-I2S-DMA.h ../src/display.h
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

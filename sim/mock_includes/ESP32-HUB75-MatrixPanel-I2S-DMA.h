#pragma once
// Mock of ESP32-HUB75-MatrixPanel-I2S-DMA for host/simulator builds.
// Only the methods actually called in display.cpp / kalshi_display.cpp are declared.

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <chrono>
#include <thread>

#define PROGMEM

inline uint32_t millis() {
    static auto _t0 = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _t0).count();
}
inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct _SerialClass {
    void println(const char* s) const { printf("%s\n", s); fflush(stdout); }
    void printf(const char* fmt, ...) const {
        va_list a; va_start(a, fmt); ::vprintf(fmt, a); va_end(a); fflush(stdout);
    }
};
inline _SerialClass Serial;

struct HUB75_I2S_CFG {
    enum driver_t { SHIFTREG = 0, ICN2038S };
    struct {
        int r1=0,r2=0,g1=0,g2=0,b1=0,b2=0,a=0,b=0,c=0,d=0,e=0,lat=0,oe=0,clk=0;
    } gpio;
    driver_t driver = SHIFTREG;
    bool clkphase = false;
    int  latch_blanking = 0;
    bool double_buff = false;
    HUB75_I2S_CFG(int /*w*/, int /*h*/, int /*chain*/) {}
};

class MatrixPanel_I2S_DMA {
public:
    explicit MatrixPanel_I2S_DMA(HUB75_I2S_CFG&);
    ~MatrixPanel_I2S_DMA();

    bool begin();
    void setBrightness8(uint8_t b);
    void clearScreen();
    void flipDMABuffer();

    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void fillScreenRGB888(uint8_t r, uint8_t g, uint8_t b);

    void setTextSize(uint8_t s);
    void setTextColor(uint16_t c);
    void setCursor(int16_t x, int16_t y);
    void print(char c);
    void print(const char* s);
};

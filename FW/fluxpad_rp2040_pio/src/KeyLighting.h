
#pragma once

#include <stdint.h>

#define FLASH_DURATION_US 50 * 1000
#define DUTY_CYCLE_ON 255
#define DUTY_CYCLE_OFF 0
#define UPDATE_PERIOD_US 20 * 1000
#define FADE_MIN_DUTY_CYCLE 2

enum LightingMode { LIGHTING_MODE_OFF, LIGHTING_MODE_STATIC, LIGHTING_MODE_FADE, LIGHTING_MODE_FLASH };

typedef struct {
    LightingMode mode;
    uint8_t brightness;
    uint32_t duration_us;
} KeyLightingSettings_t;

class KeyLighting {
  public:
    uint32_t pin = 0;
    bool &pressed_p;

    KeyLightingSettings_t settings = {};

    KeyLighting(const uint32_t pin, bool &pressed_p) : pin(pin), pressed_p(pressed_p){};

    void setup() {
        last_pressed_time_us = INT32_MAX;
        last_fade_time_us = INT32_MAX;
        pressed_prev = false;
        last_duty_cycle = DUTY_CYCLE_ON;
        set_duty_cycle(DUTY_CYCLE_OFF);
        pinMode(pin, PinMode::OUTPUT_12MA);
    }

    void applySettings(const KeyLightingSettings_t &_settings) {
        settings = _settings;
        fade_factor = pow(0.5, (static_cast<float>(UPDATE_PERIOD_US) / static_cast<float>(settings.duration_us)));
    }

    void lightingTask(uint64_t time_us) {
        bool pressed = pressed_p;
        // Serial.printf("p: %d\n", pressed);
        switch (settings.mode) {
        case LIGHTING_MODE_OFF:
            set_duty_cycle(DUTY_CYCLE_OFF);
            break;
        case LIGHTING_MODE_STATIC:
            set_duty_cycle(settings.brightness);
            break;
        case LIGHTING_MODE_FADE:
            if (pressed_p) {
                // Turn on the led
                set_duty_cycle(settings.brightness);
            } else {
                // Fade the led
                if (time_us - last_fade_time_us > UPDATE_PERIOD_US) {
                    last_fade_time_us = time_us;
                    uint8_t duty_cycle = last_duty_cycle * fade_factor;
                    duty_cycle = duty_cycle < FADE_MIN_DUTY_CYCLE ? 0 : duty_cycle;
                    set_duty_cycle(duty_cycle);
                }
            }
            break;
        case LIGHTING_MODE_FLASH:
            if (!pressed_prev && pressed) {
                last_pressed_time_us = time_us;
            }
            pressed_prev = pressed;

            if (time_us - last_pressed_time_us > settings.duration_us) {
                set_duty_cycle(DUTY_CYCLE_OFF);
            } else {
                set_duty_cycle(DUTY_CYCLE_ON);
            }

            break;
        default:
            break;
        }
    }

  private:
    bool pressed_prev;
    float fade_factor;
    uint8_t last_duty_cycle;
    uint64_t last_fade_time_us;
    uint64_t last_pressed_time_us;

    bool set_duty_cycle(uint8_t duty_cycle) {
        if (last_duty_cycle != duty_cycle) {
            analogWrite(pin, duty_cycle);
            // Serial.printf("duty: %d\n", duty_cycle);
            last_duty_cycle = duty_cycle;
            return true;
        }
        return false;
    }
};


#pragma once

// #include "FIR_filter.hpp"
#include <stdint.h>

// Q22.10 fixed point
typedef uint32_t q22_10_t;

#define Q22_10_FRAC_BITS 10
#define Q22_10_INT_BITS 22

#define INT_TO_Q22_10(x) ((x) << 10)
#define Q22_10_TO_INT(x) ((x) >> 10)
#define Q22_10_TO_FLOAT(x) ((x) / (1024.0f))
#define FLOAT_TO_Q22_10(x) static_cast<q22_10_t>((x) * (1024))

// typedef Array<fir_tap_t, FIR_NUM_TAPS> fir_taps_array_t;

// fir_taps_array_t fir_taps {0};

// ADC to Distance(mm) LUT
#define ADC_BITS 12
#define LUT_BITS 2
const q22_10_t adc_to_dist_lut[] = {
    FLOAT_TO_Q22_10(20),    // 0
    FLOAT_TO_Q22_10(9),     // 512
    FLOAT_TO_Q22_10(4.6),   // 1024
    FLOAT_TO_Q22_10(3.2),   // 1536
    FLOAT_TO_Q22_10(2.569), // 2048
    FLOAT_TO_Q22_10(2.171), // 2560
    FLOAT_TO_Q22_10(1.892), // 3072
    FLOAT_TO_Q22_10(1.677), // 3584
    FLOAT_TO_Q22_10(1.504), // 4096
};

typedef struct {
    q22_10_t press_hysteresis_mm;
    q22_10_t release_hysteresis_mm;
    q22_10_t actuation_point_mm;
    q22_10_t release_point_mm;
    uint32_t press_debounce_ms;
    uint32_t release_debounce_ms;
    uint32_t samples;
} AnalogSwitchSettings_t;

class AnalogSwitch {
  public:
    uint32_t pin = 0;
    uint32_t id = 0;

    q22_10_t current_reading = 0;
    q22_10_t current_distance_mm = 0;
    q22_10_t max_distance_mm;
    q22_10_t min_distance_mm;

    bool is_pressed = false;
    bool is_setup = false;
    bool use_freerun_mode = true;

    AnalogSwitchSettings_t settings;

    AnalogSwitch(uint32_t _pin, uint32_t _id) : pin(_pin), id(_id) {}

    void setup() {
        pinMode(pin, INPUT);
        setADCConversionTime(128, 0);
        resetMinMaxDistance();
        is_pressed = false;
        is_setup = true;
        analogRead(pin);
    }

    void applySettings(AnalogSwitchSettings_t *_settings) {
        settings = *_settings;
    }

    void takeAvgReading(size_t no_of_measurements) {
        q22_10_t sum = 0;
        for (size_t i = 0; i < no_of_measurements; i++) {
            sum += INT_TO_Q22_10(analogRead(pin));
        }
        current_reading = sum / no_of_measurements;
    }

    void mainLoopService() {
        if (use_freerun_mode) {
            takeAvgReadingFreerun(settings.samples);
        } else {
            takeAvgReading(settings.samples);
        }
        // Serial.printf("current_reading: %lu, max_reading: %lu, min_reading:
        // %lu ", current_reading, max_reading, min_reading);
        // Serial.printf("press_hys: %lu, release_hys: %lu ",
        // settings.press_hysteresis, settings.release_hysteresis);
        current_distance_mm = adcCountsToDistanceMM(current_reading);

        switch (is_pressed) {
        case false:

            // Update max distance
            if (current_distance_mm > max_distance_mm) {
                max_distance_mm = current_distance_mm;
            }

            // Check if should press
            if (max_distance_mm - current_distance_mm >
                    settings.press_hysteresis_mm ||
                current_distance_mm < settings.actuation_point_mm) {
                is_pressed = true;
                resetMinMaxDistance();
                min_distance_mm = current_distance_mm;
            };
            break;
        case true:

            // Update min distance
            if (current_distance_mm < min_distance_mm) {
                min_distance_mm = current_distance_mm;
            }

            if (current_distance_mm - min_distance_mm >
                    settings.release_hysteresis_mm ||
                current_distance_mm > settings.release_point_mm) {
                is_pressed = false;
                resetMinMaxDistance();
                max_distance_mm = current_distance_mm;
            };
            break;
        default:
            // Nothing
            break;
        }
    }

    /**
     * @brief Read n samples from pin in freerun mode
     * This is faster than getting individual samples with analogRead
     *
     * @param samples
     * @param pin
     * @return uint32_t
     */
    uint32_t takeAvgReadingFreerun(size_t samples) {
        syncADC();

        ADC->INPUTCTRL.bit.MUXPOS =
            g_APinDescription[pin].ulADCChannelNumber; // Select pos adc input
        ADC->CTRLB.bit.FREERUN = 1;                    // Turn on freerun mode

        syncADC();
        ADC->CTRLA.bit.ENABLE = 1; // Turn on ADC
        syncADC();

        ADC->SWTRIG.bit.START = 1; // Start conversion
        syncADC();

        // Throw away first result after turning on the ADC
        while (ADC->INTFLAG.bit.RESRDY == 0)
            ; // Wait for conversion to complete
        uint32_t throwaway = ADC->RESULT.reg;
        ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY; // Clear ready flag

        q22_10_t sum = 0;
        for (size_t i = 0; i < samples; i++) {
            while (ADC->INTFLAG.bit.RESRDY == 0)
                ; // Wait for conversion to complete
            uint32_t result = ADC->RESULT.reg;
            ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY; // Clear ready flag
            sum += INT_TO_Q22_10(result);
        }
        current_reading = sum / samples;

        ADC->CTRLA.bit.ENABLE = 0; // Turn off ADC
        syncADC();
    }

    // Wait for synchronization of registers between the clock domains
    static inline __attribute__((always_inline)) void syncADC() {
        while (ADC->STATUS.bit.SYNCBUSY == 1)
            ;
    }

    /**
     * @brief Setup ADC Prescaler and cycles per sample. Affects mininum
     * acceptable input impedance
     *
     * @param prescaler Clock divider setting, valid values are
     * 4,8,16,32,64,128,256,512
     * @param cyclesPerSample Number of clock cycles per sample
     */
    void setADCConversionTime(unsigned int prescaler,
                              unsigned int cyclesPerSample) {
        unsigned int my_ADC_CTRLB_PRESCALER_DIV;
        unsigned int my_SAMPCTRLREGVal;

        switch (prescaler) {
        case 4:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV4_Val;
            break;
        case 8:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV8_Val;
            break;
        case 16:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV16_Val;
            break;
        case 32:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV32_Val;
            break;
        case 64:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV64_Val;
            break;
        case 128:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV128_Val;
            break;
        case 256:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV256_Val;
            break;
        case 512:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV512_Val;
            break;
        default:
            my_ADC_CTRLB_PRESCALER_DIV = ADC_CTRLB_PRESCALER_DIV512_Val;
        }
        // ADC->CTRLB.reg = my_ADC_CTRLB_PRESCALER_DIV | ADC_CTRLB_RESSEL_12BIT;
        ADC->CTRLB.bit.PRESCALER = my_ADC_CTRLB_PRESCALER_DIV;

        while (ADC->STATUS.bit.SYNCBUSY)
            ;

        if (cyclesPerSample < 0) {
            cyclesPerSample = 0;
        }
        if (cyclesPerSample > 63) {
            cyclesPerSample = 63;
        }
        ADC->SAMPCTRL.reg = cyclesPerSample;
    }

    q22_10_t adcCountsToDistanceMM(q22_10_t counts) { return lut(counts); }

  private:
    void resetMinMaxDistance() {
        min_distance_mm = UINT32_MAX;
        max_distance_mm = 0;
    }

    /**
     * @brief Linearly interpolate input t between a and b.
     *
     * @param a If t=0, output is a
     * @param b If t=1, output is b
     * @param t Must be between 0 an 1
     * @return q22_10_t
     */
    q22_10_t lerp(q22_10_t a, q22_10_t b, q22_10_t t) {
        q22_10_t a_weight =
            a * ((1 << Q22_10_FRAC_BITS) - t) >> Q22_10_FRAC_BITS;
        q22_10_t b_weight = b * t >> Q22_10_FRAC_BITS;
        return a_weight + b_weight;
    }

    /**
     * @brief converts adc counts to distance with a lookup table
     *
     * @param x input, must be in range [0 and 2^ADC_BITS)
     * @return q22_10_t
     */
    q22_10_t lut(q22_10_t x) {
        // Get integer part of input
        size_t x_integer = Q22_10_TO_INT(x);
        // Convert ADC integer to LUT index
        size_t lut_index = x_integer >> (ADC_BITS - LUT_BITS);
        // Convert LUT index back to ADC but now it's floored
        q22_10_t x_floored = INT_TO_Q22_10(lut_index << (ADC_BITS - LUT_BITS));
        // Get difference between input and LUT index
        q22_10_t fraction = (x - x_floored) >> (ADC_BITS - LUT_BITS);
        return lerp(adc_to_dist_lut[lut_index], adc_to_dist_lut[lut_index + 1],
                    fraction);
    }
};
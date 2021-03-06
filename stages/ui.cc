// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// User interface

#include "stages/ui.h"
#include "stages/chain_state.h"

#include <algorithm>

#include "stmlib/system/system_clock.h"

using namespace std;
using namespace stmlib;

const int32_t kLongPressDuration = 500;

namespace stages {

/* static */
const MultiMode Ui::multimodes_[6] = {
  MULTI_MODE_STAGES, // Mode enabled by long pressing the left-most button
  MULTI_MODE_STAGES,
  MULTI_MODE_STAGES_SLOW_LFO,
  MULTI_MODE_SIX_EG,
  MULTI_MODE_OUROBOROS,
  MULTI_MODE_OUROBOROS_ALTERNATE, // Mode enabled by long pressing the right-most button
};

/* static */
const LedColor Ui::palette_[4] = {
  LED_COLOR_GREEN,
  LED_COLOR_YELLOW,
  LED_COLOR_RED,
  LED_COLOR_OFF
};

void Ui::Init(Settings* settings, ChainState* chain_state) {
  leds_.Init();
  switches_.Init();
  
  system_clock.Init();
  fill(&press_time_[0], &press_time_[kNumSwitches], 0);
  fill(&press_time_multimode_toggle_[0], &press_time_multimode_toggle_[kNumSwitches], 0);
  
  settings_ = settings;
  mode_ = UI_MODE_NORMAL;
  chain_state_ = chain_state;
  
  displaying_multimode_toggle_ = 0;
  displaying_multimode_toggle_pressed_ = 0;
  
  if (switches_.pressed_immediate(0)) {
    State* state = settings_->mutable_state();
    if (state->color_blind == 1) {
      state->color_blind = 0; 
    } else {
      state->color_blind = 1; 
    }
    settings_->SaveState();
  }
  
  fill(&slider_led_counter_[0], &slider_led_counter_[kNumLEDs], 0);
}

void Ui::Poll() {
  system_clock.Tick();
  UpdateLEDs();
  
  switches_.Debounce();
  
  MultiMode multimode = (MultiMode) settings_->state().multimode;
  
  if (multimode == MULTI_MODE_OUROBOROS || multimode == MULTI_MODE_OUROBOROS_ALTERNATE) {
    
    State* s = settings_->mutable_state();
    for (int i = 0; i < kNumSwitches; ++i) {
      if (switches_.pressed(i)) {
        if (press_time_[i] != -1) {
          ++press_time_[i];
        }
      } else {
        if (press_time_[i] > kLongPressDuration) { // Long-press
          if (press_time_[i] < kLongPressDurationForMultiModeToggle) { // But not long enough for multi-mode toggle
            s->segment_configuration[i] ^= 0b01000000; // Toggle waveshape MSB
            settings_->SaveState();
          }
        } else if (press_time_[i] > 0) {
          uint8_t type_bits = (s->segment_configuration[i] & 0b00110000) >> 4; // Get current waveshape LSB number
          s->segment_configuration[i] &= ~0b00110000; // Reset waveshape LSB bits
          s->segment_configuration[i] |= (((type_bits + 1) % 3) << 4); // Cycle through 0,1,2 and set LSB bits
          settings_->SaveState();
        }
        press_time_[i] = 0;
      }
    }
    
  }
  
  // Forward presses information to chain state
  ChainState::ChannelBitmask pressed = 0;
  //if (multimode == MULTI_MODE_STAGES || multimode == MULTI_MODE_STAGES_SLOW_LFO) {
    for (int i = 0; i < kNumSwitches; ++i) {
      if (switches_.pressed(i)) {
        pressed |= 1 << i;
      }
    }
  //}
  chain_state_->set_local_switch_pressed(pressed);
  
  // Detect very long presses for multi-mode toggle (using a negative counter)
  for (uint8_t i = 0; i < kNumSwitches; ++i) {
    if (switches_.pressed(i)) {
      if (press_time_multimode_toggle_[i] != -1) {
        ++press_time_multimode_toggle_[i];
      }
      if (press_time_multimode_toggle_[i] > kLongPressDurationForMultiModeToggle) {
        MultiModeToggle(i);
        press_time_multimode_toggle_[i] = -1;
      }
    } else {
      press_time_multimode_toggle_[i] = 0;
    }
  }
  
}

void Ui::MultiModeToggle(const uint8_t i) {
  
  // Save the toggle value into permanent settings (if necessary)
  State* state = settings_->mutable_state();
  if (state->multimode != (uint8_t) multimodes_[i]) {
    for (int j = 0; j < kNumSwitches; ++j) {
      press_time_[j] = -1; // Don't consider Ouroboros button presses while changing mode
    }
    chain_state_->SuspendSwitches(); // Don't consider chain button presses while changing mode
    state->multimode = (uint8_t) multimodes_[i];
    settings_->SaveState();
  }
  
  // Display visual feedback
  displaying_multimode_toggle_pressed_ = i;
  displaying_multimode_toggle_ = 1000;
  
}

inline uint8_t Ui::FadePattern(uint8_t shift, uint8_t phase) const {
  uint8_t x = system_clock.milliseconds() >> shift;
  x += phase;
  x &= 0x1f;
  return x <= 0x10 ? x : 0x1f - x;
}

void Ui::UpdateLEDs() {
  leds_.Clear();

  MultiMode multimode = (MultiMode) settings_->state().multimode;

  if (mode_ == UI_MODE_FACTORY_TEST) {
    
    size_t counter = (system_clock.milliseconds() >> 8) % 3;
    for (size_t i = 0; i < kNumChannels; ++i) {
      if (slider_led_counter_[i] == 0) {
        leds_.set(LED_GROUP_UI + i, palette_[counter]);
        leds_.set(LED_GROUP_SLIDER + i,
                  counter == 0 ? LED_COLOR_GREEN : LED_COLOR_OFF);
      } else if (slider_led_counter_[i] == 1) {
        leds_.set(LED_GROUP_UI + i, LED_COLOR_GREEN);
        leds_.set(LED_GROUP_SLIDER + i, LED_COLOR_OFF);
      } else {
        leds_.set(LED_GROUP_UI + i, LED_COLOR_GREEN);
        leds_.set(LED_GROUP_SLIDER + i, LED_COLOR_GREEN);
      }
    }
    
  } else if (chain_state_->discovering_neighbors()) {
    
    size_t counter = system_clock.milliseconds() >> 5;
    size_t n = chain_state_->size() * kNumChannels;
    counter = counter % (2 * n - 2);
    if (counter >= n) {
      counter = 2 * n - 2 - counter;
    }
    if (counter >= chain_state_->index() * kNumChannels) {
      counter -= chain_state_->index() * kNumChannels;
      if (counter < kNumChannels) {
        leds_.set(LED_GROUP_UI + counter, LED_COLOR_YELLOW);
        leds_.set(LED_GROUP_SLIDER + counter, LED_COLOR_GREEN);
      }
    }
    
  } else {
    
    if (displaying_multimode_toggle_ > 0) {
      
      // Displaying the multi-mode toggle visual feedback
      --displaying_multimode_toggle_;
      for (size_t i = 0; i < kNumChannels; ++i) {
        leds_.set(LED_GROUP_UI + i, displaying_multimode_toggle_pressed_ == i ? LED_COLOR_YELLOW : LED_COLOR_OFF);
      }
      
    } else if (
      multimode == MULTI_MODE_STAGES || multimode == MULTI_MODE_STAGES_SLOW_LFO || 
      multimode == MULTI_MODE_OUROBOROS || multimode == MULTI_MODE_OUROBOROS_ALTERNATE
    ) {
      
      // LEDs update for original Stage modes (Stages, slow LFO variant and Ouroboros)
      uint8_t pwm = system_clock.milliseconds() & 0xf;
      uint8_t fade_patterns[4] = {
        0xf,  // NONE
        FadePattern(4, 0),  // START
        FadePattern(4, 0x0f),  // END
        FadePattern(4, 0x08),  // SELF
      };
      for (size_t i = 0; i < kNumChannels; ++i) {
        uint8_t configuration = settings_->state().segment_configuration[i];
        if (multimode == MULTI_MODE_OUROBOROS || multimode == MULTI_MODE_OUROBOROS_ALTERNATE) {
          configuration = configuration >> 4; // slide to ouroboros bits
        }
        uint8_t type = configuration & 0x3;
        int brightness = fade_patterns[
          (multimode == MULTI_MODE_OUROBOROS || multimode == MULTI_MODE_OUROBOROS_ALTERNATE)
            ? (configuration & 0x4 ? 3 : 0)
            : chain_state_->loop_status(i)
          ];
        LedColor color = palette_[type];
        if (settings_->state().color_blind == 1) {
          if (type == 0) {
            color = LED_COLOR_GREEN;
            uint8_t modulation = FadePattern(6, 13 - (2 * i)) >> 1;
            brightness = brightness * (7 + modulation) >> 4;
          } else if (type == 1) {
            color = LED_COLOR_YELLOW;
            brightness = brightness >= 0x8 ? 0xf : 0;
          } else if (type == 2) {
            color = LED_COLOR_RED;
            brightness = brightness >= 0xc ? 0x1 : 0;
          }
        }
        leds_.set(
            LED_GROUP_UI + i,
            (brightness >= pwm && brightness != 0) ? color : LED_COLOR_OFF);
        leds_.set(
            LED_GROUP_SLIDER + i,
            slider_led_counter_[i] ? LED_COLOR_GREEN : LED_COLOR_OFF);
      }
      
    } else if (multimode == MULTI_MODE_SIX_EG) {
      
      // LEDs update for 6EG mode
      for (size_t i = 0; i < kNumChannels; ++i) {
        leds_.set(LED_GROUP_UI + i, led_color_[i]);
        leds_.set(LED_GROUP_SLIDER + i, slider_led_counter_[i] ? LED_COLOR_GREEN : LED_COLOR_OFF);
      }
      
    } else {
      
      // Invalid mode, turn all off
      for (size_t i = 0; i < kNumChannels; ++i) {
        leds_.set(LED_GROUP_UI + i, LED_COLOR_OFF);
        leds_.set(LED_GROUP_SLIDER + i, LED_COLOR_OFF);
      }
      
    }
    
    // For any multi-mode, update slider LEDs counters
    for (size_t i = 0; i < kNumChannels; ++i) {
      if (slider_led_counter_[i]) {
        --slider_led_counter_[i];
      }
    }
    
  }
  
  leds_.Write();
  
}

}  // namespace stages

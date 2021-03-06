#include "utils/avr_timer_counter.h"

#include "utils/counting_print.h"
#include "utils/inline_literal.h"
#include "utils/logging.h"
#include "utils/print_misc.h"
#include "utils/stream_to_print.h"
#include "utils/traits/print_to_trait.h"
#include "utils/traits/type_traits.h"

namespace alpaca {

size_t PrintValueTo(ClockPrescaling v, Print& out) {
  auto flash_string = ToFlashStringHelper(v);
  if (flash_string != nullptr) {
    return out.print(flash_string);
  }
  return PrintUnknownEnumValueTo(TAS_FLASHSTR("ClockPrescaling"),
                                 static_cast<uint32_t>(v), out);
}

size_t PrintValueTo(FastPwmCompareOutputMode v, Print& out) {
  auto flash_string = ToFlashStringHelper(v);
  if (flash_string != nullptr) {
    return out.print(flash_string);
  }
  return PrintUnknownEnumValueTo(TAS_FLASHSTR("FastPwmCompareOutputMode"),
                                 static_cast<uint32_t>(v), out);
}

size_t PrintValueTo(TimerCounterChannel v, Print& out) {
  auto flash_string = ToFlashStringHelper(v);
  if (flash_string != nullptr) {
    return out.print(flash_string);
  }
  return PrintUnknownEnumValueTo(TAS_FLASHSTR("TimerCounterChannel"),
                                 static_cast<uint32_t>(v), out);
}

////////////////////////////////////////////////////////////////////////////////

const __FlashStringHelper* ToFlashStringHelper(ClockPrescaling v) {
  switch (v) {
    case ClockPrescaling::kDisabled:
      return TAS_FLASHSTR("Disabled");
    case ClockPrescaling::kDivideBy1:
      return TAS_FLASHSTR("DivideBy1");
    case ClockPrescaling::kDivideBy8:
      return TAS_FLASHSTR("DivideBy8");
    case ClockPrescaling::kDivideBy64:
      return TAS_FLASHSTR("DivideBy64");
    case ClockPrescaling::kDivideBy256:
      return TAS_FLASHSTR("DivideBy256");
    case ClockPrescaling::kDivideBy1024:
      return TAS_FLASHSTR("DivideBy1024");
  }
  return nullptr;
}

const __FlashStringHelper* ToFlashStringHelper(FastPwmCompareOutputMode v) {
  switch (v) {
    case FastPwmCompareOutputMode::kDisabled:
      return TAS_FLASHSTR("Disabled");
    case FastPwmCompareOutputMode::kNonInvertingMode:
      return TAS_FLASHSTR("NonInvertingMode");
    case FastPwmCompareOutputMode::kInvertingMode:
      return TAS_FLASHSTR("InvertingMode");
  }
  return nullptr;
}

const __FlashStringHelper* ToFlashStringHelper(TimerCounterChannel v) {
  switch (v) {
    case TimerCounterChannel::A:
      return TAS_FLASHSTR("A");
    case TimerCounterChannel::B:
      return TAS_FLASHSTR("B");
    case TimerCounterChannel::C:
      return TAS_FLASHSTR("C");
  }
  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

uint16_t ToClockDivisor(ClockPrescaling prescaling) {
  switch (prescaling) {
    case ClockPrescaling::kDivideBy1:
      return 1;
    case ClockPrescaling::kDivideBy8:
      return 8;
    case ClockPrescaling::kDivideBy64:
      return 64;
    case ClockPrescaling::kDivideBy256:
      return 256;
    case ClockPrescaling::kDivideBy1024:
      return 1024;
    default:
      return 0;
  }
}

TC16ClockAndTicks TC16ClockAndTicks::FromSystemClockCycles(
    uint32_t system_clock_cycles) {
  TAS_VLOG(5) << TAS_FLASHSTR("FromSystemClockCycles ") << system_clock_cycles;
  TAS_DCHECK_LE(system_clock_cycles, kMaxSystemClockCycles)
      << TAS_FLASHSTR("system_clock_cycles: ") << system_clock_cycles
      << TAS_FLASHSTR("  kMaxSystemClockCycles: ") << kMaxSystemClockCycles;
  if (system_clock_cycles <= kMaxClockTicks) {
    if (system_clock_cycles == 0) {
      return {.clock_select = ClockPrescaling::kDisabled, .clock_ticks = 0};
    }
    return {.clock_select = ClockPrescaling::kDivideBy1,
            .clock_ticks = static_cast<uint16_t>(system_clock_cycles)};
  } else if (system_clock_cycles <= kMaxClockTicks * 8) {
    return {.clock_select = ClockPrescaling::kDivideBy8,
            .clock_ticks = static_cast<uint16_t>(system_clock_cycles / 8)};
  } else if (system_clock_cycles <= kMaxClockTicks * 64) {
    return {.clock_select = ClockPrescaling::kDivideBy64,
            .clock_ticks = static_cast<uint16_t>(system_clock_cycles / 64)};
  } else if (system_clock_cycles <= kMaxClockTicks * 256) {
    return {.clock_select = ClockPrescaling::kDivideBy256,
            .clock_ticks = static_cast<uint16_t>(system_clock_cycles / 256)};
  } else if (system_clock_cycles <= kMaxClockTicks * 1024) {
    return {.clock_select = ClockPrescaling::kDivideBy1024,
            .clock_ticks = static_cast<uint16_t>(system_clock_cycles / 1024)};
  } else {
    TAS_DCHECK(false) << TAS_FLASHSTR("system_clock_cycles: ")
                      << system_clock_cycles
                      << TAS_FLASHSTR("  kMaxSystemClockCycles: ")
                      << kMaxSystemClockCycles;
    return {.clock_select = ClockPrescaling::kDisabled, .clock_ticks = 0};
  }
}

TC16ClockAndTicks TC16ClockAndTicks::FromMicroSeconds(uint32_t us) {
  // Convert a duration (us) into the number of system clock cycles for
  // approximately that same duration; it is accurate if an integral number of
  // cycles fits into a microsecond. The following somewhat convoluted
  // expression deals with the precision issues of multiplying and dividing
  // fixed bit size integers. For example (F_CPU / 100,000) is the number of
  // clock cycles in 10 microseconds.
  const uint32_t system_clock_cycles = ((F_CPU / 100000) * us) / 10;
  return FromSystemClockCycles(system_clock_cycles);
}

TC16ClockAndTicks TC16ClockAndTicks::FromNanoSeconds(uint32_t ns) {
  // Working in nanoseconds risks overflow in the 32-bit calculations of the
  // number of clock cycles, therefore I'm dividing this up into separate cases
  // for short periods and long periods. I'm assuming (and verifying) here that
  // kShortPeriodLimit is far longer than a system clock cycle.
  constexpr uint32_t kShortPeriodLimit = (UINT32_MAX - 0) / (F_CPU / 100000);
  static_assert(kShortPeriodLimit / kNanoSecondsPerSystemClockCycle > 100,
                "kShortPeriodLimit is too small");

  constexpr uint32_t half_cycle_ns = 500000000UL / F_CPU;
  if ((ns + half_cycle_ns) >= kShortPeriodLimit) {
    // Round ns to the nearest microsecond.
    return FromMicroSeconds((ns + 500) / 1000);
  }

  // Convert a duration (ns) into the number of system clock cycles that most
  // closely represents the same duration. Some rounding is inevitable because
  // we expect that for microcontrollers the system clock cycle is far long than
  // a nanosecond); we explicitly round ns up by one half of a system clock
  // cycle.
  ns += half_cycle_ns;
  // The following somewhat convoluted expression deals with the precision
  // issues of multiplying and dividing fixed bit size integers. For example
  // (F_CPU / 100,000) is the number of clock cycles in 10 microseconds.
  const uint32_t system_clock_cycles = ((F_CPU / 100000) * ns) / 10000;
  return FromSystemClockCycles(system_clock_cycles);
}

TC16ClockAndTicks TC16ClockAndTicks::FromSeconds(double s) {
  TAS_DCHECK_LE(s, kMaxSeconds);
  if (s > kMaxSeconds) {
    return {.clock_select = ClockPrescaling::kDisabled, .clock_ticks = 0};
  }
  double cycles = s * F_CPU + 0.5;
  TAS_DCHECK_LE(cycles, UINT32_MAX);
  return FromSystemClockCycles(cycles);
}

TC16ClockAndTicks TC16ClockAndTicks::FromIntegerEventsPerSecond(
    uint16_t events) {
  TAS_DCHECK_GT(events, 0);
  if (events > 0) {
    return FromSystemClockCycles(F_CPU / events);
  }
  return {.clock_select = ClockPrescaling::kDisabled, .clock_ticks = 0};
}

TC16ClockAndTicks TC16ClockAndTicks::FromDoubleEventsPerSecond(double events) {
  TAS_DCHECK_GT(events, 0);
  if (events <= 0) {
    return {.clock_select = ClockPrescaling::kDisabled, .clock_ticks = 0};
  }
  return FromSystemClockCycles(F_CPU / events);
}

uint32_t TC16ClockAndTicks::ToSystemClockCycles() const {
  return static_cast<uint32_t>(clock_ticks) * ToClockDivisor(clock_select);
}

double TC16ClockAndTicks::ToSeconds() const {
  return ToSystemClockCycles() / static_cast<double>(F_CPU);
}

size_t TC16ClockAndTicks::printTo(Print& out) const {
  static_assert(has_print_to<decltype(*this)>{}, "has_print_to should be true");
  CountingPrint counter(out);
  counter << TAS_FLASHSTR("{.cs=") << clock_select << TAS_FLASHSTR(", .ticks=")
          << clock_ticks << '}';
  return counter.count();
}

////////////////////////////////////////////////////////////////////////////////

namespace {
constexpr uint8_t kKeepAllExceptChannelA =
    static_cast<uint8_t>(~(0b11 << COM1A0));
constexpr uint8_t kKeepAllExceptChannelB =
    static_cast<uint8_t>(~(0b11 << COM1B0));
constexpr uint8_t kKeepAllExceptChannelC =
    static_cast<uint8_t>(~(0b11 << COM1C0));
}  // namespace

void TimerCounter1Initialize16BitFastPwm(ClockPrescaling prescaling) {
  noInterrupts();
  TCCR1A = 1 << WGM11;
  TCCR1B =
      (1 << WGM13) | (1 << WGM12) | static_cast<uint8_t>(prescaling) << CS10;
  ICR1 = UINT16_MAX;
  interrupts();
}

void TimerCounter1SetCompareOutputMode(TimerCounterChannel channel,
                                       FastPwmCompareOutputMode mode) {
  uint8_t keep_mask, set_mask;
  switch (channel) {
    case TimerCounterChannel::A:
      keep_mask = kKeepAllExceptChannelA;
      set_mask = static_cast<uint8_t>(mode) << COM1A0;
      break;
    case TimerCounterChannel::B:
      keep_mask = kKeepAllExceptChannelB;
      set_mask = static_cast<uint8_t>(mode) << COM1B0;
      break;
    case TimerCounterChannel::C:
      keep_mask = kKeepAllExceptChannelC;
      set_mask = static_cast<uint8_t>(mode) << COM1C0;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
  noInterrupts();
  TCCR1A = (TCCR1A & keep_mask) | set_mask;
  interrupts();
}

void TimerCounter1SetOutputCompareRegister(TimerCounterChannel channel,
                                           uint16_t value) {
  switch (channel) {
    case TimerCounterChannel::A:
      OCR1A = value;
      break;
    case TimerCounterChannel::B:
      OCR1B = value;
      break;
    case TimerCounterChannel::C:
      OCR1C = value;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
}

uint16_t TimerCounter1GetOutputCompareRegister(TimerCounterChannel channel) {
  switch (channel) {
    case TimerCounterChannel::A:
      return OCR1A;
    case TimerCounterChannel::B:
      return OCR1B;
    case TimerCounterChannel::C:
      return OCR1C;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
      return 0;
  }
}

////////////////////////////////////////////////////////////////////////////////

void TimerCounter3Initialize16BitFastPwm(ClockPrescaling prescaling) {
  noInterrupts();
  TCCR3A = 1 << WGM11;
  TCCR3B =
      (1 << WGM13) | (1 << WGM12) | static_cast<uint8_t>(prescaling) << CS10;
  ICR3 = UINT16_MAX;
  interrupts();
}

void TimerCounter3SetCompareOutputMode(TimerCounterChannel channel,
                                       FastPwmCompareOutputMode mode) {
  uint8_t keep_mask, set_mask;
  switch (channel) {
    case TimerCounterChannel::A:
      keep_mask = kKeepAllExceptChannelA;
      set_mask = static_cast<uint8_t>(mode) << COM3A0;
      break;
    case TimerCounterChannel::B:
      keep_mask = kKeepAllExceptChannelB;
      set_mask = static_cast<uint8_t>(mode) << COM3B0;
      break;
    case TimerCounterChannel::C:
      keep_mask = kKeepAllExceptChannelC;
      set_mask = static_cast<uint8_t>(mode) << COM3C0;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
  noInterrupts();
  TCCR3A = (TCCR3A & keep_mask) | set_mask;
  interrupts();
}

void TimerCounter3SetOutputCompareRegister(TimerCounterChannel channel,
                                           uint16_t value) {
  switch (channel) {
    case TimerCounterChannel::A:
      OCR3A = value;
      break;
    case TimerCounterChannel::B:
      OCR3B = value;
      break;
    case TimerCounterChannel::C:
      OCR3C = value;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
}

uint16_t TimerCounter3GetOutputCompareRegister(TimerCounterChannel channel) {
  switch (channel) {
    case TimerCounterChannel::A:
      return OCR3A;
    case TimerCounterChannel::B:
      return OCR3B;
    case TimerCounterChannel::C:
      return OCR3C;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
      return 0;
  }
}

////////////////////////////////////////////////////////////////////////////////

void TimerCounter4Initialize16BitFastPwm(ClockPrescaling prescaling) {
  noInterrupts();
  TCCR4A = 1 << WGM11;
  TCCR4B =
      (1 << WGM13) | (1 << WGM12) | static_cast<uint8_t>(prescaling) << CS10;
  ICR4 = UINT16_MAX;
  interrupts();
}

void TimerCounter4SetCompareOutputMode(TimerCounterChannel channel,
                                       FastPwmCompareOutputMode mode) {
  uint8_t keep_mask, set_mask;
  switch (channel) {
    case TimerCounterChannel::A:
      keep_mask = kKeepAllExceptChannelA;
      set_mask = static_cast<uint8_t>(mode) << COM4A0;
      break;
    case TimerCounterChannel::B:
      keep_mask = kKeepAllExceptChannelB;
      set_mask = static_cast<uint8_t>(mode) << COM4B0;
      break;
    case TimerCounterChannel::C:
      keep_mask = kKeepAllExceptChannelC;
      set_mask = static_cast<uint8_t>(mode) << COM4C0;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
  noInterrupts();
  TCCR4A = (TCCR4A & keep_mask) | set_mask;
  interrupts();
}

void TimerCounter4SetOutputCompareRegister(TimerCounterChannel channel,
                                           uint16_t value) {
  switch (channel) {
    case TimerCounterChannel::A:
      OCR4A = value;
      break;
    case TimerCounterChannel::B:
      OCR4B = value;
      break;
    case TimerCounterChannel::C:
      OCR4C = value;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
}

uint16_t TimerCounter4GetOutputCompareRegister(TimerCounterChannel channel) {
  switch (channel) {
    case TimerCounterChannel::A:
      return OCR4A;
    case TimerCounterChannel::B:
      return OCR4B;
    case TimerCounterChannel::C:
      return OCR4C;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
      return 0;
  }
}

////////////////////////////////////////////////////////////////////////////////

void TimerCounter5Initialize16BitFastPwm(ClockPrescaling prescaling) {
  noInterrupts();
  TCCR5A = 1 << WGM11;
  TCCR5B =
      (1 << WGM13) | (1 << WGM12) | static_cast<uint8_t>(prescaling) << CS10;
  ICR5 = UINT16_MAX;
  interrupts();
}

void TimerCounter5SetCompareOutputMode(TimerCounterChannel channel,
                                       FastPwmCompareOutputMode mode) {
  uint8_t keep_mask, set_mask;
  switch (channel) {
    case TimerCounterChannel::A:
      keep_mask = kKeepAllExceptChannelA;
      set_mask = static_cast<uint8_t>(mode) << COM5A0;
      break;
    case TimerCounterChannel::B:
      keep_mask = kKeepAllExceptChannelB;
      set_mask = static_cast<uint8_t>(mode) << COM5B0;
      break;
    case TimerCounterChannel::C:
      keep_mask = kKeepAllExceptChannelC;
      set_mask = static_cast<uint8_t>(mode) << COM5C0;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
  noInterrupts();
  TCCR5A = (TCCR5A & keep_mask) | set_mask;
  interrupts();
}

void TimerCounter5SetOutputCompareRegister(TimerCounterChannel channel,
                                           uint16_t value) {
  switch (channel) {
    case TimerCounterChannel::A:
      OCR5A = value;
      break;
    case TimerCounterChannel::B:
      OCR5B = value;
      break;
    case TimerCounterChannel::C:
      OCR5C = value;
      break;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
  }
}

uint16_t TimerCounter5GetOutputCompareRegister(TimerCounterChannel channel) {
  switch (channel) {
    case TimerCounterChannel::A:
      return OCR5A;
    case TimerCounterChannel::B:
      return OCR5B;
    case TimerCounterChannel::C:
      return OCR5C;
    default:
      TAS_DCHECK(false) << TAS_FLASHSTR("Unknown channel ") << channel;
      return 0;
  }
}

////////////////////////////////////////////////////////////////////////////////

EnableableByPin::EnableableByPin(uint8_t enabled_pin)
    : enabled_pin_(enabled_pin) {}

EnableableByPin::EnableableByPin() : enabled_pin_(kNoEnabledPin) {}

bool EnableableByPin::IsEnabled() const {
  if (enabled_pin_ == kNoEnabledPin) {
    return true;
  }
  pinMode(enabled_pin_, INPUT_PULLUP);
  bool result = digitalRead(enabled_pin_) == LOW;
  pinMode(enabled_pin_, INPUT);  // Avoid supplying current unncessarily.
  return result;
}

int EnableableByPin::ReadPin() const {
  if (enabled_pin_ == kNoEnabledPin) {
    return -1;
  }
  pinMode(enabled_pin_, INPUT_PULLUP);
  delayMicroseconds(50);
  int result = digitalRead(enabled_pin_);
  pinMode(enabled_pin_, INPUT);  // Avoid supplying current unncessarily.
  return result;
}

////////////////////////////////////////////////////////////////////////////////

TimerCounter1Pwm16Output::TimerCounter1Pwm16Output(TimerCounterChannel channel,
                                                   uint8_t enabled_pin)
    : EnableableByPin(enabled_pin), channel_(channel) {}
TimerCounter1Pwm16Output::TimerCounter1Pwm16Output(TimerCounterChannel channel)
    : channel_(channel) {}

void TimerCounter1Pwm16Output::set_pulse_count(uint16_t value) {
  TAS_DCHECK(IsEnabled());
  if (value == 0) {
    TimerCounter1SetCompareOutputMode(channel_,
                                      FastPwmCompareOutputMode::kDisabled);
  } else {
    TimerCounter1SetCompareOutputMode(
        channel_, FastPwmCompareOutputMode::kNonInvertingMode);
    TimerCounter1SetOutputCompareRegister(channel_, value);
  }
}

uint16_t TimerCounter1Pwm16Output::get_pulse_count() const {
  TAS_DCHECK(IsEnabled());
  return TimerCounter1GetOutputCompareRegister(channel_);
}

////////////////////////////////////////////////////////////////////////////////

TimerCounter3Pwm16Output::TimerCounter3Pwm16Output(TimerCounterChannel channel,
                                                   uint8_t enabled_pin)
    : EnableableByPin(enabled_pin), channel_(channel) {}
TimerCounter3Pwm16Output::TimerCounter3Pwm16Output(TimerCounterChannel channel)
    : channel_(channel) {}

void TimerCounter3Pwm16Output::set_pulse_count(uint16_t value) {
  TAS_DCHECK(IsEnabled());
  if (value == 0) {
    TimerCounter3SetCompareOutputMode(channel_,
                                      FastPwmCompareOutputMode::kDisabled);
  } else {
    TimerCounter3SetCompareOutputMode(
        channel_, FastPwmCompareOutputMode::kNonInvertingMode);
    TimerCounter3SetOutputCompareRegister(channel_, value);
  }
}

uint16_t TimerCounter3Pwm16Output::get_pulse_count() const {
  TAS_DCHECK(IsEnabled());
  return TimerCounter3GetOutputCompareRegister(channel_);
}

////////////////////////////////////////////////////////////////////////////////

TimerCounter4Pwm16Output::TimerCounter4Pwm16Output(TimerCounterChannel channel,
                                                   uint8_t enabled_pin)
    : EnableableByPin(enabled_pin), channel_(channel) {}
TimerCounter4Pwm16Output::TimerCounter4Pwm16Output(TimerCounterChannel channel)
    : channel_(channel) {}

void TimerCounter4Pwm16Output::set_pulse_count(uint16_t value) {
  TAS_DCHECK(IsEnabled());
  if (value == 0) {
    TimerCounter4SetCompareOutputMode(channel_,
                                      FastPwmCompareOutputMode::kDisabled);
  } else {
    TimerCounter4SetCompareOutputMode(
        channel_, FastPwmCompareOutputMode::kNonInvertingMode);
    TimerCounter4SetOutputCompareRegister(channel_, value);
  }
}

uint16_t TimerCounter4Pwm16Output::get_pulse_count() const {
  TAS_DCHECK(IsEnabled());
  return TimerCounter4GetOutputCompareRegister(channel_);
}

////////////////////////////////////////////////////////////////////////////////

TimerCounter5Pwm16Output::TimerCounter5Pwm16Output(TimerCounterChannel channel,
                                                   uint8_t enabled_pin)
    : EnableableByPin(enabled_pin), channel_(channel) {}
TimerCounter5Pwm16Output::TimerCounter5Pwm16Output(TimerCounterChannel channel)
    : channel_(channel) {}

void TimerCounter5Pwm16Output::set_pulse_count(uint16_t value) {
  TAS_DCHECK(IsEnabled());
  if (value == 0) {
    TimerCounter5SetCompareOutputMode(channel_,
                                      FastPwmCompareOutputMode::kDisabled);
  } else {
    TimerCounter5SetCompareOutputMode(
        channel_, FastPwmCompareOutputMode::kNonInvertingMode);
    TimerCounter5SetOutputCompareRegister(channel_, value);
  }
}

uint16_t TimerCounter5Pwm16Output::get_pulse_count() const {
  TAS_DCHECK(IsEnabled());
  return TimerCounter5GetOutputCompareRegister(channel_);
}

}  // namespace alpaca

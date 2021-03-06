#include "utils/print_misc.h"

#include "utils/counting_print.h"
#include "utils/inline_literal.h"
#include "utils/platform.h"

namespace alpaca {

size_t PrintUnknownEnumValueTo(const __FlashStringHelper* name, uint32_t v,
                               Print& out) {
  CountingPrint counter(out);
  counter.print(TAS_FLASHSTR("Undefined "));
  counter.print(name);
  counter.print(' ');
  counter.print('(');
  counter.print(v);
  counter.print(')');
  return counter.count();
}

}  // namespace alpaca

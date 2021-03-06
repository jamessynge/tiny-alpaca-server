#ifndef TINY_ALPACA_SERVER_EXTRAS_TEST_TOOLS_JSON_DECODER_H_
#define TINY_ALPACA_SERVER_EXTRAS_TEST_TOOLS_JSON_DECODER_H_

// A trivial JSON decoder, intended only to support testing whether the
// responses from Tiny Alpaca Server are correct.

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "utils/logging.h"

namespace alpaca {
namespace test {

class JsonValue;

using JsonObjectBase = std::unordered_map<std::string, JsonValue>;
using JsonArrayBase = std::vector<JsonValue>;

class JsonObject : public JsonObjectBase {
 public:
  using JsonObjectBase::JsonObjectBase;

  template <typename T>
  JsonObject& Add(std::string_view key, T t);

  bool HasKey(const std::string& key) const;
  JsonValue GetValue(const std::string& key) const;
};

class JsonArray : public JsonArrayBase {
 public:
  using JsonArrayBase::JsonArrayBase;

  template <typename T>
  JsonArray& Add(T t);
};

class JsonValue {
 public:
  // This order must match the order of types in the variant value_'s
  // declaration.
  enum EType {
    kUnset = 0,  // This instance does NOT contain a value.
    kNull,
    kBool,
    kNumber,
    kString,
    kObject,
    kArray,
  };

  JsonValue();  // Has no value.
  explicit JsonValue(nullptr_t);
  explicit JsonValue(bool);
  explicit JsonValue(double);
  explicit JsonValue(int);  // NOLINT
  explicit JsonValue(const std::string&);
  explicit JsonValue(std::string_view);
  explicit JsonValue(const char*);
  explicit JsonValue(std::shared_ptr<JsonObject>);
  explicit JsonValue(JsonObject);
  explicit JsonValue(std::shared_ptr<JsonArray>);
  explicit JsonValue(JsonArray);

  // Parse str as a JSON, returning a JsonValue if successful, else an error
  // status.
  static absl::StatusOr<JsonValue> Parse(std::string_view str);

  // Returns the type of value stored.
  EType type() const;
  bool is_unset() const { return type() == kUnset; }
  bool is_null() const { return type() == kNull; }
  bool is_bool() const { return type() == kBool; }
  bool is_number() const { return type() == kNumber; }
  bool is_string() const { return type() == kString; }
  bool is_object() const { return type() == kObject; }
  bool is_array() const { return type() == kArray; }

  // The as_X methods will raise an exception if the wrong method is called, so
  // be sure to use type() first.
  bool as_bool() const;
  double as_number() const;
  const std::string& as_string() const;
  const JsonObject& as_object() const;
  const JsonArray& as_array() const;

  // The following methods allow for tests to be written without too much
  // disassembly of the JSON object/arrays that a payload may contain. These
  // would not be suitable for a production JSON application.

  // If this value is an object, returns true iff the object has the specified
  // key; else returns false.
  bool HasKey(const std::string& key) const;

  // If this value is an object, returns the value of the specified key. Returns
  // a JsonValue of type kUnset if this value is not an object or does not
  // have the key.
  JsonValue GetValue(const std::string& key) const;

  // Returns true this value is an array with the specified index, else returns
  // false.
  bool HasIndex(size_t index) const;

  // If this value is an array and the array's size is greater than index,
  // returns the value of the specified index in the array; otherwise returns a
  // JsonValue of type kUnset.
  JsonValue GetElement(size_t index) const;

  friend std::ostream& operator<<(std::ostream& os, const JsonValue& jv);

 private:
  struct Undefined {};

  // This order must match the order in EType
  std::variant<Undefined, nullptr_t, bool, double, std::string,
               std::shared_ptr<JsonObject>, std::shared_ptr<JsonArray>>
      value_;
};

template <typename T>
JsonObject& JsonObject::Add(std::string_view key, T t) {
  JsonValue value(t);
  TAS_CHECK(value.type() != JsonValue::kUnset);
  insert_or_assign(std::string(key), value);
  return *this;
}

template <typename T>
JsonArray& JsonArray::Add(T t) {
  JsonValue value(t);
  TAS_CHECK(value.type() != JsonValue::kUnset);
  push_back(value);
  return *this;
}

bool operator==(const JsonValue& jv, nullptr_t);
inline bool operator==(nullptr_t, const JsonValue& jv) { return jv == nullptr; }

bool operator==(const JsonValue& jv, bool b);
inline bool operator==(bool b, const JsonValue& jv) { return jv == b; }

bool operator==(const JsonValue& jv, double d);
inline bool operator==(double d, const JsonValue& jv) { return jv == d; }

bool operator==(const JsonValue& jv, std::string_view s);
inline bool operator==(std::string_view s, const JsonValue& jv) {
  return jv == s;
}

bool operator==(const JsonValue& jv, const JsonArray& v);
inline bool operator==(const JsonArray& v, const JsonValue& jv) {
  return jv == v;
}

bool operator==(const JsonValue& jv, const JsonObject& v);
inline bool operator==(const JsonObject& v, const JsonValue& jv) {
  return jv == v;
}

bool operator==(const JsonValue& a, const JsonValue& b);
inline bool operator!=(const JsonValue& a, const JsonValue& b) {
  return !(a == b);
}

template <typename T>
bool operator!=(const JsonValue& a, T b) {
  return !(a == b);
}
template <typename T>
bool operator!=(T a, const JsonValue& b) {
  return !(b == a);
}

}  // namespace test
}  // namespace alpaca

#endif  // TINY_ALPACA_SERVER_EXTRAS_TEST_TOOLS_JSON_DECODER_H_

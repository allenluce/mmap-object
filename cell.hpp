#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <nan.h>

namespace bip=boost::interprocess;
using namespace std;

typedef bip::managed_shared_memory::segment_manager segment_manager_t;

template <typename StorageType> using SharedAllocator =
  bip::allocator<StorageType, segment_manager_t>;

typedef SharedAllocator<char> char_allocator;

typedef bip::basic_string<char, char_traits<char>, char_allocator> shared_string;

// Types of cells
#define UNINITIALIZED 0
#define STRING_TYPE 1
#define NUMBER_TYPE 2
#define BUFFER_TYPE 3

class Cell {
private:
  char cell_type;
  union values {
    shared_string string_value;
    double number_value;
    values(const char *value, const shared_string::size_type len, char_allocator allocator): string_value(value, len, allocator) {}
    values(const char *value, char_allocator allocator): string_value(value, allocator) {}
    values(const double value): number_value(value) {}
    values() {}
    ~values() {}
  } cell_value;
  Cell& operator =(const Cell&) = default;
  Cell(Cell&&) = default;
  Cell& operator=(Cell&&) & = default;
public:
  Cell(const char *value, const shared_string::size_type len, char_allocator allocator) : cell_type(BUFFER_TYPE), cell_value(value, len, allocator) {}
  Cell(const char *value, char_allocator allocator) : cell_type(STRING_TYPE), cell_value(value, allocator) {}
  Cell(const double value) : cell_type(NUMBER_TYPE), cell_value(value) {}
  Cell(const Cell &cell);
  ~Cell() {
    if (cell_type == STRING_TYPE || cell_type == BUFFER_TYPE)
      cell_value.string_value.~shared_string();
  }
  char type() { return cell_type; }
  shared_string::size_type length() { return cell_value.string_value.length(); }
  const char *c_str();
  operator double();
  v8::Local<v8::Value> GetValue(); 
  static size_t SetValue(v8::Local<v8::Value> value, bip::managed_mapped_file *segment, unique_ptr<Cell> &c, const Nan::PropertyCallbackInfo<v8::Value>& info);
};

class WrongPropertyType: public exception {};
class FileTooLarge: public exception {};


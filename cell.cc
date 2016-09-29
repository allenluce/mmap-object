#include <nan.h>
#include "cell.hpp"
#include "common.hpp"

// Avoid freeing shared memory
static void NullFreer(char *, void *) {}

const char *Cell::c_str() {
  if (type() != STRING_TYPE && type() != BUFFER_TYPE)
    throw WrongPropertyType();
  return cell_value.string_value.c_str();
}

Cell::operator double() {
  if (type() != NUMBER_TYPE)
    throw WrongPropertyType();
  return cell_value.number_value;
}

Cell::Cell(const Cell &cell) {
  cell_type = cell.cell_type;
  switch (cell_type) {
  case STRING_TYPE:
  case BUFFER_TYPE:
    new (&cell_value.string_value)(shared_string)(cell.cell_value.string_value);
    break;
  case NUMBER_TYPE:
    cell_value.number_value = cell.cell_value.number_value;
    break;
  default:
    throw WrongPropertyType();
  }
}

v8::Local<v8::Value> Cell::GetValue() {
  v8::Local<v8::Value> v;
  switch (type()) {
  case STRING_TYPE:
    v = Nan::New<v8::String>(c_str()).ToLocalChecked();
    break;
  case BUFFER_TYPE:
    v = Nan::NewBuffer(const_cast<char*>(c_str()), length(), NullFreer, NULL).ToLocalChecked();
    break;
  case NUMBER_TYPE:
    v = Nan::New<v8::Number>(*this);
    break;
  default:
    ostringstream error_stream;
    error_stream << "Unknown cell data type " << dec << (int) type();
    Nan::ThrowError(error_stream.str().c_str());
  }
  return v;
}

// Estimate length of the data that will be stored in a SetValue() call.
// This to give a working estimate for file growing.
size_t Cell::ValueLength(v8::Local<v8::Value> value) {
  size_t length;
  if (value->IsString()) {
    v8::String::Utf8Value data UTF8VALUE(value);
    length = data.length();
  } else if (value->IsNumber()) {
    length = sizeof(double);
  } else if (value->IsArrayBufferView()) {
    v8::Local<v8::Object> buf = value->ToObject();
    length = node::Buffer::Length(buf);
  } else {
    Nan::ThrowError("Value must be a string, buffer, or number.");
    return -1;
  }
  return length;
}

// Create a new cell to wrap the given value with, reset the given
// unique_ptr to that cell. Return the length of the data stored for
// the caller's accounting.
void Cell::SetValue(v8::Local<v8::Value> value, bip::managed_mapped_file *segment,
                      unique_ptr<Cell> &c, const Nan::PropertyCallbackInfo<v8::Value>& info) {
  if (value->IsString()) {
    v8::String::Utf8Value data UTF8VALUE(value);
    char_allocator allocer(segment->get_segment_manager());
    c.reset(new Cell(string(*data).c_str(), allocer));
  } else if (value->IsNumber()) {
    c.reset(new Cell(Nan::To<double>(value).FromJust()));
  } else if (value->IsArrayBufferView()) {
    v8::Local<v8::Object> buf = value->ToObject();
    char* bufData = node::Buffer::Data(buf);
    size_t bufLen = node::Buffer::Length(buf);
    char_allocator allocer(segment->get_segment_manager());
    c.reset(new Cell(bufData, bufLen, allocer));
  } else {
    Nan::ThrowError("Value must be a string, buffer, or number.");
  }
}

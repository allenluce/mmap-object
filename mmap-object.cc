#include <stdbool.h>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/unordered_map.hpp>
#include <nan.h>

#define MINIMUM_FILE_SIZE 500 // Minimum necessary to handle an mmap'd unordered_map on all platforms.
#define DEFAULT_FILE_SIZE 5ul<<20 // 5 megs
#define DEFAULT_MAX_SIZE 5000ul<<20 // 5000 megs

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

namespace bip=boost::interprocess;
using namespace std;

typedef bip::managed_shared_memory::segment_manager segment_manager_t;

template <typename StorageType> using SharedAllocator =
  bip::allocator<StorageType, segment_manager_t>;

typedef SharedAllocator<char> char_allocator;

typedef bip::basic_string<char, char_traits<char>, char_allocator> shared_string;
typedef bip::basic_string<char, char_traits<char>> char_string;

#define UNINITIALIZED 0
#define STRING_TYPE 1
#define NUMBER_TYPE 2
#define OBJECT_TYPE 3
class WrongPropertyType: public exception {};
class FileTooLarge: public exception {};

class Cell {
private:
  char cell_type;
  union values {
    shared_string string_value;
    double number_value;
    values(const char *value, char_allocator allocator): string_value(value, allocator) {}
    values(const double value): number_value(value) {}
    values() {}
    ~values() {}
  } cell_value;
  Cell& operator =(const Cell&) = default;
  Cell(Cell&&) = default;
  Cell& operator=(Cell&&) & = default;
public:
  Cell(const char *value, char_allocator allocator, char type) : cell_type(type), cell_value(value, allocator) {}
  Cell(const double value) : cell_type(NUMBER_TYPE), cell_value(value) {}
  Cell(const Cell &cell);
  char type() { return cell_type; }
  const char *c_str();
  operator string();
  operator double();
};

typedef shared_string KeyType;
typedef Cell ValueType;

typedef SharedAllocator<pair<KeyType, ValueType>> map_allocator;

struct s_equal_to {
  bool operator()( const char_string& lhs, const shared_string& rhs ) const {
    return string(lhs.c_str()) == string(rhs.c_str());
  }
  bool operator()( const shared_string& lhs, const shared_string& rhs ) const {
    return string(lhs.c_str()) == string(rhs.c_str());
  }
};

class hasher {
public:
  size_t operator() (shared_string const& key) const {
    return boost::hash<shared_string>()(key);
  }
  size_t operator() (char_string const& key) const {
    return boost::hash<char_string>()(key);
  }
};

typedef boost::unordered_map<
  KeyType,
  ValueType,
  hasher,
  s_equal_to,
  map_allocator> PropertyHash;

class SharedMap : public Nan::ObjectWrap {
public:
  static NAN_MODULE_INIT(Init);

private:
  string file_name;
  size_t file_size;
  size_t max_file_size;
  bip::managed_mapped_file *map_seg;
  PropertyHash *property_map;
  bool readonly;
  bool closed;
  void grow(size_t);
  void reconnect();
  void setFilename(string);
  static NAN_METHOD(Create);
  static NAN_METHOD(Open);
  static NAN_METHOD(Close);
  static NAN_METHOD(isClosed);
  static NAN_METHOD(isOpen);
  static NAN_METHOD(get_free_memory);
  static NAN_METHOD(get_size);
  static NAN_METHOD(bucket_count);
  static NAN_METHOD(max_bucket_count);
  static NAN_METHOD(load_factor);
  static NAN_METHOD(max_load_factor);
  static NAN_PROPERTY_SETTER(PropSetter);
  static NAN_PROPERTY_GETTER(PropGetter);
  static NAN_PROPERTY_QUERY(PropQuery);
  static NAN_PROPERTY_ENUMERATOR(PropEnumerator);
  static NAN_PROPERTY_DELETER(PropDeleter);
  static NAN_INDEX_GETTER(IndexGetter);
  static NAN_INDEX_SETTER(IndexSetter);
  static NAN_INDEX_DELETER(IndexDeleter);
  static NAN_INDEX_QUERY(IndexQuery);
  static v8::Local<v8::Function> init_methods(v8::Local<v8::FunctionTemplate> f_tpl);
  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }
};

const char *Cell::c_str() {
  if (type() != STRING_TYPE && type() != OBJECT_TYPE)
    throw WrongPropertyType();
 return cell_value.string_value.c_str();
}

Cell::operator string() {
  if (type() != STRING_TYPE && type() != OBJECT_TYPE)
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
  if (cell.cell_type == STRING_TYPE || cell.cell_type == OBJECT_TYPE) {
    new (&cell_value.string_value)(shared_string)(cell.cell_value.string_value, cell.cell_value.string_value.get_allocator());
  } else { // is a number type
    cell_value.number_value = cell.cell_value.number_value;
  }
}

NAN_PROPERTY_SETTER(SharedMap::PropSetter) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  if (self->readonly) {
    Nan::ThrowError("Read-only object.");
    return;
  }

  if (self->closed) {
    Nan::ThrowError("Cannot write to closed object.");
    return;
  }

  if (property->IsSymbol()) {
    Nan::ThrowError("Symbol properties are not supported.");
    return;
  }

  size_t data_length = sizeof(Cell);

  try {
    Cell *c;
    while(true) {
      try {
        if (value->IsObject() || value->IsArray()) {
          v8::Local<v8::Value> args[] = { value };

          v8::Isolate* isolate = info.GetIsolate();
          v8::Locker locker(isolate);
          v8::Isolate::Scope isolateScope(isolate);
          if (isolate->InContext()) {
            v8::Local<v8::Object> global = isolate->GetCurrentContext()->Global();
            v8::Local<v8::Object> JSON = v8::Local<v8::Object>::Cast(
              global->Get(Nan::New<v8::String>("JSON").ToLocalChecked()));
            v8::Local<v8::Function> stringify = v8::Local<v8::Function>::Cast(
              JSON->Get(Nan::New<v8::String>("stringify").ToLocalChecked()));

            v8::Local<v8::String> result = stringify->Call(JSON, 1, args)->ToString();
            v8::String::Utf8Value data(result);
            data_length += data.length();
            char_allocator allocer(self->map_seg->get_segment_manager());
            c = new Cell(string(*data).c_str(), allocer, OBJECT_TYPE);
          }
        } else if (value->IsString()) {
          v8::String::Utf8Value data(value);
          data_length += data.length();
          char_allocator allocer(self->map_seg->get_segment_manager());
          c = new Cell(string(*data).c_str(), allocer, STRING_TYPE);
        } else if (value->IsNumber()) {
          data_length += sizeof(double);
          c = new Cell(Nan::To<double>(value).FromJust());
        } else if (!(value->IsUndefined() || value->IsNull())) {
          Nan::ThrowError("Value must be a string or number.");
          return;
        }

        v8::String::Utf8Value prop(property);
        data_length += prop.length();
        shared_string *string_key;
        char_allocator allocer(self->map_seg->get_segment_manager());
        string_key = new shared_string(string(*prop).c_str(), allocer);
        if (value->IsUndefined() || value->IsNull()) {
          self->property_map->erase(*string_key);
        }
        else {
          auto pair = self->property_map->insert({ *string_key, *c });
          if (!pair.second) {
            self->property_map->erase(*string_key);
            self->property_map->insert({ *string_key, *c });
          }
        }
        info.GetReturnValue().Set(value);
        break;
      } catch(std::length_error) {
        self->grow(data_length * 2);
      } catch(bip::bad_alloc) {
        self->grow(data_length * 2);
      }
    }
  } catch(FileTooLarge) {
    Nan::ThrowError("File grew too large.");
  } catch(exception& ex) {
    Nan::ThrowError(ex.what());
    self->reconnect();
  }
}

NAN_PROPERTY_GETTER(SharedMap::PropGetter) {
  v8::String::Utf8Value data(info.Data());
  v8::String::Utf8Value src(property);
  if (string(*data) == "prototype" ||
    string(*src) == "close" ||
    string(*src) == "isClosed" ||
    string(*src) == "isOpen" ||
    string(*src) == "valueOf" ||
    string(*src) == "toString") {
    return;
  }

  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());

  if (self->closed) {
    Nan::ThrowError("Cannot read from closed object.");
    return;
  }

  try {
    // If the map doesn't have it, let v8 continue the search.
    auto pair = self->property_map->find<char_string, hasher, s_equal_to>
      (*src, hasher(), s_equal_to());

    if (pair == self->property_map->end())
      return;
    Cell *c = &pair->second;
    if (c->type() == OBJECT_TYPE) {
      v8::Local<v8::Value> args[] = { Nan::New<v8::String>(c->c_str()).ToLocalChecked() };

      v8::Isolate* isolate = info.GetIsolate();
      v8::Locker locker(isolate);
      v8::Isolate::Scope isolateScope(isolate);

      if (isolate->InContext()) {
        v8::EscapableHandleScope handle_scope(isolate);
        v8::TryCatch trycatch(isolate);
        v8::Local<v8::Object> global = isolate->GetCurrentContext()->Global();
        v8::Local<v8::Object> JSON = v8::Local<v8::Object>::Cast(
          global->Get(Nan::New<v8::String>("JSON").ToLocalChecked()));
        v8::Local<v8::Function> parse = v8::Local<v8::Function>::Cast(
          JSON->Get(Nan::New<v8::String>("parse").ToLocalChecked()));

        v8::Local<v8::Value> result = handle_scope.Escape(parse->Call(JSON, 1, args));
        if (!trycatch.HasCaught()) {
          info.GetReturnValue().Set(result);
        }
        else {
          trycatch.Reset();
          // @todo memory error and wrong string to parse on reader when writer performs grow()
          self->reconnect();
        }
      }
    }
    else if (c->type() == STRING_TYPE) {
      info.GetReturnValue().Set(Nan::New<v8::String>(c->c_str()).ToLocalChecked());
    } else if (c->type() == NUMBER_TYPE) {
      info.GetReturnValue().Set((double)*c);
    }
  } catch (exception& ex) {
    Nan::ThrowError(ex.what());
    self->reconnect();
  }
}

NAN_PROPERTY_QUERY(SharedMap::PropQuery) {
  v8::String::Utf8Value data(info.Data());
  v8::String::Utf8Value src(property);
  if (string(*data) == "prototype" ||
    string(*src) == "close" ||
    string(*src) == "isClosed" ||
    string(*src) == "isOpen" ||
    string(*src) == "valueOf" ||
    string(*src) == "toString") {
    return;
  }

  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());

  if (self->closed) {
    Nan::ThrowError("Cannot query from closed object.");
    return;
  }

  if (property->IsSymbol()) {
    Nan::ThrowError("Symbol properties are not supported.");
    return;
  }

  try {
    // If the map doesn't have it, let v8 continue the search.
    auto pair = self->property_map->find<char_string, hasher, s_equal_to>
      (*src, hasher(), s_equal_to());

    if (pair == self->property_map->end()) {
      return;
    }
    info.GetReturnValue().Set(Nan::New<v8::Integer>(v8::None));
  } catch (exception& ex) {
    Nan::ThrowError(ex.what());
    self->reconnect();
  }
}

NAN_PROPERTY_DELETER(SharedMap::PropDeleter) {
  v8::String::Utf8Value data(info.Data());
  v8::String::Utf8Value src(property);

  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());

  if (self->readonly) {
    Nan::ThrowError("Read-only object.");
    return;
  }

  if (self->closed) {
    Nan::ThrowError("Cannot delete from closed object.");
    return;
  }

  if (property->IsSymbol()) {
    Nan::ThrowError("Symbol properties are not supported.");
    return;
  }

  try {
    v8::String::Utf8Value prop(property);
    shared_string *string_key;
    char_allocator allocer(self->map_seg->get_segment_manager());
    string_key = new shared_string(string(*prop).c_str(), allocer);
    self->property_map->erase(*string_key);
    info.GetReturnValue().Set(true);
  } catch (exception& ex) {
    Nan::ThrowError(ex.what());
    self->reconnect();
  }
}

NAN_PROPERTY_ENUMERATOR(SharedMap::PropEnumerator) {
  v8::Local<v8::Array> arr = Nan::New<v8::Array>();
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());

  if (self->closed) {
    Nan::ThrowError("Cannot enumerate closed object.");
    return;
  }

  try {
    int i = 0;
    for (auto it = self->property_map->begin(); it != self->property_map->end(); ++it) {
      arr->Set(i++, Nan::New<v8::String>(it->first.c_str()).ToLocalChecked());
    }
    info.GetReturnValue().Set(arr);
  } catch (exception& ex) {
    Nan::ThrowError(ex.what());
    self->reconnect();
  }
}

NAN_INDEX_GETTER(SharedMap::IndexGetter) {
  // Nan::ThrowError("Shared object is not indexable.");
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  v8::Local<v8::String> property = v8::Local<v8::String>::Cast(Nan::New<v8::Uint32>(index));
  return self->PropGetter(property, info);
}

NAN_INDEX_SETTER(SharedMap::IndexSetter) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  v8::Local<v8::String> property = v8::Local<v8::String>::Cast(Nan::New<v8::Uint32>(index));
  return self->PropSetter(property, value, info);
}

NAN_INDEX_QUERY(SharedMap::IndexQuery) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  v8::Local<v8::String> property = v8::Local<v8::String>::Cast(Nan::New<v8::Uint32>(index));
  return self->PropQuery(property, info);
}

NAN_INDEX_DELETER(SharedMap::IndexDeleter) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  v8::Local<v8::String> property = v8::Local<v8::String>::Cast(Nan::New<v8::Uint32>(index));
  return self->PropDeleter(property, info);
}

NAN_INDEX_ENUMERATOR(IndexEnumerator) {}

#define INFO_METHOD(name, type, object) NAN_METHOD(SharedMap::name) { \
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This()); \
  info.GetReturnValue().Set((type)self->object->name()); \
}

INFO_METHOD(get_free_memory, uint32_t, map_seg)
INFO_METHOD(get_size, uint32_t, map_seg)
INFO_METHOD(bucket_count, uint32_t, property_map)
INFO_METHOD(max_bucket_count, uint32_t, property_map)
INFO_METHOD(load_factor, float, property_map)
INFO_METHOD(max_load_factor, float, property_map)

NAN_METHOD(SharedMap::Create) {
  if (!info.IsConstructCall()) {
    Nan::ThrowError("Create must be called as a constructor.");
    return;
  }

  Nan::Utf8String filename(info[0]->ToString());
  size_t file_size = (int)info[1]->Int32Value();
  size_t initial_bucket_count = (int)info[2]->Int32Value();
  size_t max_file_size = (int)info[3]->Int32Value();
  SharedMap *d = new SharedMap();

  if (file_size == 0) {
    file_size = DEFAULT_FILE_SIZE;
  }
  // Don't open it too small.
  if (file_size < MINIMUM_FILE_SIZE) {
    file_size = 500;
    max_file_size = max(file_size, max_file_size);
  }
  if (max_file_size == 0) {
    max_file_size = DEFAULT_MAX_SIZE;
  }

  // Default to 1024 buckets
  if (initial_bucket_count == 0) {
    initial_bucket_count = 1024;
  }

  try {
    d->map_seg = new bip::managed_mapped_file(bip::open_or_create,string(*filename).c_str(), file_size);
    d->property_map = d->map_seg->find_or_construct<PropertyHash>("properties")
      (initial_bucket_count, hasher(), s_equal_to(), d->map_seg->get_segment_manager());
  } catch(bip::interprocess_exception &ex){
    ostringstream error_stream;
    error_stream << "Can't open file " << *filename << ": " << ex.what();
    Nan::ThrowError(error_stream.str().c_str());
    return;
  }

  d->readonly = false;
  d->closed = false;
  d->setFilename(*filename);
  d->file_size = file_size;
  d->max_file_size = max_file_size;
  d->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
}

NAN_METHOD(SharedMap::Open) {
  if (!info.IsConstructCall()) {
    Nan::ThrowError("Open must be called as a constructor.");
    return;
  }

  Nan::Utf8String filename(info[0]->ToString());
  SharedMap *d = new SharedMap();

  struct stat buf;
  int s = stat(*filename, &buf);
  if (!S_ISREG(buf.st_mode)) {
    ostringstream error_stream;
    error_stream << *filename;
    if (s) {
      error_stream << " does not exist.";
    } else {
      error_stream << " is not a regular file.";
    }
    Nan::ThrowError(error_stream.str().c_str());
    return;
  }

  try {
    d->map_seg = new bip::managed_mapped_file(bip::open_read_only, string(*filename).c_str());
    auto find_map = d->map_seg->find<PropertyHash>("properties");
    d->property_map = find_map.first;
  } catch(bip::interprocess_exception &ex){
    ostringstream error_stream;
    error_stream << "Can't open file " << *filename << ": " << ex.what();
    Nan::ThrowError(error_stream.str().c_str());
    return;
  }
  d->readonly = true;
  d->closed = false;
  d->setFilename(*filename);
  d->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
}


void SharedMap::setFilename(string fn_string) {
  file_name = fn_string;
}

void SharedMap::reconnect() {
  map_seg->flush();
  delete map_seg;
  map_seg = new bip::managed_mapped_file(bip::open_only, file_name.c_str());
  property_map = map_seg->find<PropertyHash>("properties").first;
  closed = false;
}

void SharedMap::grow(size_t size) {
  file_size += size;
  if (file_size > max_file_size) {
    throw FileTooLarge();
  }
  map_seg->flush();
  delete map_seg;
  bip::managed_mapped_file::grow(file_name.c_str(), size);
  map_seg = new bip::managed_mapped_file(bip::open_only, file_name.c_str());
  property_map = map_seg->find<PropertyHash>("properties").first;
  closed = false;
}

NAN_METHOD(SharedMap::Close) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  if (self->closed) {
    return;
  }
  self->closed = true;
  bool shrink = (self->map_seg->get_size() > self->file_size);
  if (!self->readonly) {
    self->map_seg->flush();
  }
  delete self->map_seg;
  self->map_seg = NULL;
  if (!self->readonly && shrink) {
    try {
      bip::managed_mapped_file::shrink_to_fit(self->file_name.c_str());
    } catch (exception& ex) {
      Nan::ThrowError(ex.what());
    }
  }
}

NAN_METHOD(SharedMap::isClosed) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  info.GetReturnValue().Set(self->closed);
}

NAN_METHOD(SharedMap::isOpen) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  info.GetReturnValue().Set(!self->closed);
}

v8::Local<v8::Function> SharedMap::init_methods(v8::Local<v8::FunctionTemplate> f_tpl) {
  Nan::SetPrototypeMethod(f_tpl, "close", Close);
  Nan::SetPrototypeMethod(f_tpl, "isClosed", isClosed);
  Nan::SetPrototypeMethod(f_tpl, "isOpen", isOpen);
  Nan::SetPrototypeMethod(f_tpl, "get_free_memory", get_free_memory);
  Nan::SetPrototypeMethod(f_tpl, "get_size", get_size);
  Nan::SetPrototypeMethod(f_tpl, "bucket_count", bucket_count);
  Nan::SetPrototypeMethod(f_tpl, "max_bucket_count", max_bucket_count);
  Nan::SetPrototypeMethod(f_tpl, "load_factor", load_factor);
  Nan::SetPrototypeMethod(f_tpl, "max_load_factor", max_load_factor);

  auto proto = f_tpl->PrototypeTemplate();
  Nan::SetNamedPropertyHandler(proto, PropGetter, PropSetter, PropQuery, PropDeleter, PropEnumerator,
                               Nan::New<v8::String>("prototype").ToLocalChecked());

  auto inst = f_tpl->InstanceTemplate();
  inst->SetInternalFieldCount(1);
  Nan::SetNamedPropertyHandler(inst, PropGetter, PropSetter, PropQuery, PropDeleter, PropEnumerator,
                               Nan::New<v8::String>("instance").ToLocalChecked());
  Nan::SetIndexedPropertyHandler(inst, IndexGetter, IndexSetter, IndexQuery, IndexDeleter, IndexEnumerator,
                                 Nan::New<v8::String>("instance").ToLocalChecked());

  auto fun = Nan::GetFunction(f_tpl).ToLocalChecked();
  constructor().Reset(fun);
  return fun;
}

NAN_MODULE_INIT(SharedMap::Init) {
  // The mmap creator class
  v8::Local<v8::FunctionTemplate> create_tpl = Nan::New<v8::FunctionTemplate>(Create);
  create_tpl->SetClassName(Nan::New("CreateMmap").ToLocalChecked());
  auto create_fun = init_methods(create_tpl);
  Nan::Set(target, Nan::New("Create").ToLocalChecked(), create_fun);

  // The mmap opener class
  v8::Local<v8::FunctionTemplate> open_tpl = Nan::New<v8::FunctionTemplate>(Open);
  open_tpl->SetClassName(Nan::New("OpenMmap").ToLocalChecked());
  auto open_fun = init_methods(open_tpl);
  Nan::Set(target, Nan::New("Open").ToLocalChecked(), open_fun);
}

NODE_MODULE(mmap_object, SharedMap::Init)

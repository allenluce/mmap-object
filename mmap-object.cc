#include <stdbool.h>
#include <nan.h>
#include "mmap-object.h"

__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");

bool s_equal_to::operator()( const char_string& lhs, const shared_string& rhs ) const {
  return string(lhs.c_str()) == string(rhs.c_str());
}

bool s_equal_to::operator()( const shared_string& lhs, const shared_string& rhs ) const {
  return string(lhs.c_str()) == string(rhs.c_str());
}

size_t hasher::operator() (shared_string const& key) const {
  return boost::hash<shared_string>()(key);
}

size_t hasher::operator() (char_string const& key) const {
  return boost::hash<char_string>()(key);
}

const char *Cell::c_str() {
  if (type() != STRING_TYPE)
    throw WrongPropertyType();
 return cell_value.string_value.c_str();
}

Cell::operator string() {
  if (type() != STRING_TYPE)
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
  if (cell.cell_type == STRING_TYPE) {
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
        if (value->IsString()) {
          v8::String::Utf8Value data(value);
          data_length += data.length();
          char_allocator allocer(self->map_seg->get_segment_manager());
          c = new Cell(string(*data).c_str(), allocer);
        } else if (value->IsNumber()) {
          data_length += sizeof(double);
          c = new Cell(Nan::To<double>(value).FromJust());
        } else {
          Nan::ThrowError("Value must be a string or number.");
          return;
        }
        
        v8::String::Utf8Value prop(property);
        data_length += prop.length();
        shared_string *string_key;
        char_allocator allocer(self->map_seg->get_segment_manager());
        string_key = new shared_string(string(*prop).c_str(), allocer);
        auto pair = self->property_map->insert({*string_key, *c});
        if (!pair.second) {
          self->property_map->erase(*string_key);
          self->property_map->insert({*string_key, *c});
        }
        break;
      } catch(std::length_error) {
        self->grow(data_length * 2);
      } catch(bip::bad_alloc) {
        self->grow(data_length * 2);
      }
    }
  } catch(FileTooLarge) {
    Nan::ThrowError("File grew too large.");
  }
}

NAN_PROPERTY_GETTER(SharedMap::PropGetter) {
  v8::String::Utf8Value data(info.Data());
  v8::String::Utf8Value src(property);
  if (string(*data) == "prototype")
    return;
  if (string(*src) == "isClosed")
    return;
  if (string(*src) == "valueOf")
    return;
  if (string(*src) == "toString")
    return;

  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());

  if (self->closed) {
    Nan::ThrowError("Cannot read from closed object.");
    return;
  }

  // If the map doesn't have it, let v8 continue the search.
  auto pair = self->property_map->find<char_string, hasher, s_equal_to>
    (*src, hasher(), s_equal_to());
  
  if (pair == self->property_map->end())
    return;
  Cell *c = &pair->second;
  if (c->type() == STRING_TYPE) {
    info.GetReturnValue().Set(Nan::New<v8::String>(c->c_str()).ToLocalChecked());
  } else if (c->type() == NUMBER_TYPE) {
    info.GetReturnValue().Set((double)*c);
  }
}

NAN_PROPERTY_QUERY(PropQuery) {}
NAN_PROPERTY_DELETER(PropDeleter) {}

NAN_PROPERTY_ENUMERATOR(PropEnumerator) {
  v8::Local<v8::Array> arr = Nan::New<v8::Array>();
  Nan::Set(arr, 0, Nan::New("value").ToLocalChecked());
  info.GetReturnValue().Set(arr);
}

NAN_INDEX_GETTER(IndexGetter) {
  Nan::ThrowError("Shared object is not indexable.");
}

NAN_INDEX_SETTER(IndexSetter) {
  Nan::ThrowError("Shared object is not indexable.");
}

NAN_INDEX_QUERY(IndexQuery) {
  Nan::ThrowError("Shared object is not indexable.");
}

NAN_INDEX_DELETER(IndexDeleter) {
  Nan::ThrowError("Shared object is not indexable.");
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
    d->map_seg = new bip::managed_mapped_file(bip::create_only,string(*filename).c_str(), file_size);
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
  bip::managed_mapped_file::shrink_to_fit(self->file_name.c_str());
  self->map_seg->flush();
  delete self->map_seg;
  self->map_seg = NULL;
  self->closed = true;
}

NAN_METHOD(SharedMap::isClosed) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMap>(info.This());
  info.GetReturnValue().Set(self->closed);
}

v8::Local<v8::Function> SharedMap::init_methods(v8::Local<v8::FunctionTemplate> f_tpl) {
  Nan::SetPrototypeMethod(f_tpl, "close", Close);
  Nan::SetPrototypeMethod(f_tpl, "isClosed", isClosed);
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

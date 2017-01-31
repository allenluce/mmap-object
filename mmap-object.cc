#if defined(__linux__)
  #include <features.h>
  #if defined(__GLIBC_PREREQ)
    #if __GLIBC_PREREQ(2, 13)
      __asm__(".symver clock_gettime,clock_gettime@GLIBC_2.2.5");
      __asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
    #endif
  #endif
#endif
#include <stdbool.h>
#include <assert.h>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/scope_exit.hpp>
#include <boost/thread/thread_time.hpp>
#include <boost/unordered_map.hpp>
#include <boost/version.hpp>
#include <nan.h>

#define LOCKINFO(lock) // cout << ::getpid() << " LOCK " << lock << endl
#define UNLOCKINFO(lock) // cout << ::getpid() << " UNLOCK " << lock << endl

#if BOOST_VERSION < 105500
#pragma message("Found boost version " BOOST_PP_STRINGIZE(BOOST_LIB_VERSION))
#error mmap-object needs at least version 1_55 to maintain compatibility.
#endif

#define MINIMUM_FILE_SIZE 1024 // Minimum necessary to handle an mmap'd unordered_map on all platforms.
#define DEFAULT_FILE_SIZE 5ul<<20 // 5 megs
#define DEFAULT_MAX_SIZE 5000ul<<20 // 5000 megs

// For Win32 compatibility
#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

#if NODE_MODULE_VERSION < IOJS_3_0_MODULE_VERSION
typedef v8::Handle<v8::Object> ADDON_REGISTER_FUNCTION_ARGS2_TYPE;
#else
typedef v8::Local<v8::Value> ADDON_REGISTER_FUNCTION_ARGS2_TYPE;
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
class WrongPropertyType: public exception {};

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
  Cell(const char *value, char_allocator allocator) : cell_type(STRING_TYPE), cell_value(value, allocator) {}
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

typedef bip::interprocess_upgradable_mutex upgradable_mutex_type;

struct Mutexes {
  upgradable_mutex_type rw_mutex;
  upgradable_mutex_type wo_mutex;
};  

class SharedMap : public Nan::ObjectWrap {
  friend class SharedMapControl;
  friend struct CloseWorker;
public:
  SharedMap() : readonly(false), writeonly(false), closed(false), inWriteLock(false) {}
  virtual ~SharedMap();
  void reify_mutexes();
  static NAN_MODULE_INIT(Init) {
    auto tpl = Nan::New<v8::FunctionTemplate>();
    auto inst = tpl->InstanceTemplate();
    inst->SetInternalFieldCount(1);
    Nan::SetNamedPropertyHandler(inst, PropGetter, PropSetter, PropQuery, PropDeleter, PropEnumerator,
                                 Nan::New<v8::String>("prototype").ToLocalChecked());
    Nan::SetIndexedPropertyHandler(inst, IndexGetter, IndexSetter, IndexQuery, IndexDeleter, IndexEnumerator,
                                   Nan::New<v8::String>("instance").ToLocalChecked());
    constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
  }

  static v8::Local<v8::Object> NewInstance() {
    v8::Local<v8::Function> cons = Nan::New(constructor());
    return Nan::NewInstance(cons).ToLocalChecked();
  }
  
private:
  string file_name;
  size_t max_file_size;
  bip::managed_mapped_file *map_seg;
  PropertyHash *property_map;
  size_t initial_bucket_count;
  bool readonly;
  bool writeonly;
  bool closed;
  
  bool inWriteLock;
  bip::mapped_region mutex_region;
  Mutexes *mutexes;
  
  void setFilename(string);
  bool grow(size_t);
  static SharedMap* unwrap(v8::Local<v8::Object>);
  static NAN_PROPERTY_SETTER(PropSetter);
  static NAN_PROPERTY_GETTER(PropGetter);
  static NAN_PROPERTY_QUERY(PropQuery);
  static NAN_PROPERTY_ENUMERATOR(PropEnumerator);
  static NAN_PROPERTY_DELETER(PropDeleter);
  static NAN_INDEX_GETTER(IndexGetter);
  static NAN_INDEX_SETTER(IndexSetter);
  static NAN_INDEX_QUERY(IndexQuery);
  static NAN_INDEX_DELETER(IndexDeleter);
  static NAN_INDEX_ENUMERATOR(IndexEnumerator);
  static NAN_METHOD(inspect);
  static v8::Local<v8::Function> init_methods(v8::Local<v8::FunctionTemplate> f_tpl);
  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }
};

class SharedMapControl : public Nan::ObjectWrap {
public:
  SharedMapControl(SharedMap* m) : map{m} {};
  static void Init(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE exports, ADDON_REGISTER_FUNCTION_ARGS2_TYPE module);
private:
  SharedMap *map;

  static SharedMapControl* unwrap(v8::Local<v8::Object>);
  static NAN_METHOD(Open);
  static NAN_METHOD(Close);
  static NAN_METHOD(isClosed);
  static NAN_METHOD(isOpen);
  static NAN_METHOD(writeLock);
  static NAN_METHOD(writeUnlock);
  static NAN_METHOD(get_free_memory);
  static NAN_METHOD(get_size);
  static NAN_METHOD(bucket_count);
  static NAN_METHOD(max_bucket_count);
  static NAN_METHOD(load_factor);
  static NAN_METHOD(max_load_factor);
  static NAN_METHOD(remove_shared_mutex);
  static v8::Local<v8::Function> init_methods(v8::Local<v8::FunctionTemplate> f_tpl);
  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }
};

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

SharedMapControl* SharedMapControl::unwrap(v8::Local<v8::Object> thisObj) {
  if (thisObj->InternalFieldCount() != 1 || thisObj->IsUndefined()) {
    Nan::ThrowError("Not actually an mmap object!");
    return NULL;
  }
  return Nan::ObjectWrap::Unwrap<SharedMapControl>(thisObj);
}

SharedMap* SharedMap::unwrap(v8::Local<v8::Object> thisObj) {
  if (thisObj->InternalFieldCount() != 1 || thisObj->IsUndefined()) {
    Nan::ThrowError("Not actually an mmap object!");
    return NULL;
  }
  return Nan::ObjectWrap::Unwrap<SharedMap>(thisObj);
}

NAN_PROPERTY_SETTER(SharedMap::PropSetter) {
  auto self = unwrap(info.This());
  if (!self) {
    return;
  }
  if (self->readonly) {
    Nan::ThrowError("Cannot write to read-only object.");
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
  bip::scoped_lock<upgradable_mutex_type> lock;
  
  if (!self->inWriteLock) {
    LOCKINFO("WRITE 1");
    bip::scoped_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex);
    lock.swap(lock_);
  }

  BOOST_SCOPE_EXIT(self) {
    if (!self->inWriteLock) {
      UNLOCKINFO("WRITE 1");
    }
  } BOOST_SCOPE_EXIT_END;
  
  if (value->IsString()) {
    v8::String::Utf8Value data(value);
    data_length += data.length();
  } else if (value->IsNumber()) {
    data_length += sizeof(double);
  } else {
    Nan::ThrowError("Value must be a string or number.");
    return;
  }
  v8::String::Utf8Value prop(property);
  Cell *c = 0;
  while(true) {
    try {
      char_allocator allocer(self->map_seg->get_segment_manager());
      if (value->IsString()) {
        v8::String::Utf8Value data(value);
        c = new Cell(string(*data).c_str(), allocer); // ALLOC
      } else if (value->IsNumber()) {
        c = new Cell(Nan::To<double>(value).FromJust());
      }
      shared_string *string_key;
      string_key = new shared_string(string(*prop).c_str(), allocer);
      auto pair = self->property_map->insert({ *string_key, *c }); // ALLOC
      if (!pair.second) {
        self->property_map->erase(*string_key);
        self->property_map->insert({ *string_key, *c });
      }
      delete c;
      break;
    } catch (bip::lock_exception &ex) {
      ostringstream error_stream;
      error_stream << "Lock exception: " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      return;
    } catch(length_error) {
      if (!self->grow(data_length)) {
        return;
      }
    } catch(bip::bad_alloc) {
      delete c;
      if (!self->grow(data_length + 8000)) {
        return;
      }
    }
  }
  info.GetReturnValue().Set(value);
}

#define STRINGINDEX                                             \
  std::ostringstream ss;                                        \
  ss << index;                                                  \
  auto prop = Nan::New<v8::String>(ss.str()).ToLocalChecked()

NAN_INDEX_GETTER(SharedMap::IndexGetter) {
  STRINGINDEX;
  SharedMap::PropGetter(prop, info);
}

NAN_INDEX_SETTER(SharedMap::IndexSetter) {
  STRINGINDEX;
  SharedMap::PropSetter(prop, value, info);
}

NAN_INDEX_QUERY(SharedMap::IndexQuery) {
  STRINGINDEX;
  SharedMap::PropQuery(prop, info);
}

NAN_INDEX_DELETER(SharedMap::IndexDeleter) {
  STRINGINDEX;
  SharedMap::PropDeleter(prop, info);
}

NAN_INDEX_ENUMERATOR(SharedMap::IndexEnumerator) {
  info.GetReturnValue().Set(Nan::New<v8::Array>(v8::None));
}

NAN_METHOD(SharedMap::inspect) {
  info.GetReturnValue().Set(v8::None);
}

NAN_PROPERTY_GETTER(SharedMap::PropGetter) {
  if (property->IsSymbol())
    return;
  v8::String::Utf8Value src(property);
  if (property->IsSymbol() || string(*src) == "prototype")
    return;

  auto self = unwrap(info.This());
  if (!self)
    return;

  if (self->closed) {
    Nan::ThrowError("Cannot read from closed object.");
    return;
  }
  bip::sharable_lock<upgradable_mutex_type> lock;
  if (!self->inWriteLock) {
    LOCKINFO("SHARE 1");
    bip::sharable_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex);
    lock.swap(lock_);
  }
  
  BOOST_SCOPE_EXIT(self) {
    if (!self->inWriteLock) {
      UNLOCKINFO("SHARE 1");
    }
  } BOOST_SCOPE_EXIT_END;

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

NAN_PROPERTY_QUERY(SharedMap::PropQuery) {
  v8::String::Utf8Value src(property);

  auto self = unwrap(info.This());
  if (!self)
    return;

  if (self->readonly) {
    info.GetReturnValue().Set(Nan::New<v8::Integer>(v8::ReadOnly | v8::DontDelete));
    return;
  }
    
  info.GetReturnValue().Set(Nan::New<v8::Integer>(v8::None));
}

NAN_PROPERTY_DELETER(SharedMap::PropDeleter) {
  if (property->IsSymbol()) {
    Nan::ThrowError("Symbol properties are not supported for delete.");
    return;
  }
  
  v8::String::Utf8Value src(property);

  auto self = unwrap(info.This());
  if (!self)
    return;

  if (self->readonly) {
    Nan::ThrowError("Cannot delete from read-only object.");
    return;
  }

  if (self->closed) {
    Nan::ThrowError("Cannot delete from closed object.");
    return;
  }
  bip::scoped_lock<upgradable_mutex_type> lock;
  if (!self->inWriteLock) {
    LOCKINFO("WRITE 2");
    bip::scoped_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex);
    lock.swap(lock_);
  }

  BOOST_SCOPE_EXIT(self) {
    if (!self->inWriteLock) {
      UNLOCKINFO("WRITE 2");
    }
  } BOOST_SCOPE_EXIT_END;
    
  v8::String::Utf8Value prop(property);
  shared_string *string_key;
  char_allocator allocer(self->map_seg->get_segment_manager());
  string_key = new shared_string(string(*prop).c_str(), allocer);
  self->property_map->erase(*string_key);
}

NAN_PROPERTY_ENUMERATOR(SharedMap::PropEnumerator) {
  v8::Local<v8::Array> arr = Nan::New<v8::Array>();
  auto self = unwrap(info.This());
  if (!self)
    return;

  if (self->closed) {
    info.GetReturnValue().Set(Nan::New<v8::Array>(v8::None));
    return;
  }

  int i = 0;
  bip::sharable_lock<upgradable_mutex_type> lock(self->mutexes->rw_mutex);
  for (auto it = self->property_map->begin(); it != self->property_map->end(); ++it) {
	  arr->Set(i++, Nan::New<v8::String>(it->first.c_str()).ToLocalChecked());
  }
  info.GetReturnValue().Set(arr);
}

#define INFO_METHOD(name, type, object) NAN_METHOD(SharedMapControl::name) { \
    auto self = unwrap(info.This());                                    \
    if (!self) return;                                                  \
    bip::sharable_lock<upgradable_mutex_type> lock(self->map->mutexes->rw_mutex); \
    info.GetReturnValue().Set((type)self->map->object->name());         \
  }

INFO_METHOD(get_free_memory, uint32_t, map_seg)
INFO_METHOD(get_size, uint32_t, map_seg)
INFO_METHOD(bucket_count, uint32_t, property_map)
INFO_METHOD(max_bucket_count, uint32_t, property_map)
INFO_METHOD(load_factor, float, property_map)
INFO_METHOD(max_load_factor, float, property_map)

NAN_METHOD(SharedMapControl::remove_shared_mutex) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  string mutex_name(self->map->file_name);
  replace( mutex_name.begin(), mutex_name.end(), '/', '-');
  bip::shared_memory_object::remove(mutex_name.c_str());
}

void SharedMap::reify_mutexes() {
  string mutex_name(file_name);
  replace(mutex_name.begin(), mutex_name.end(), '/', '-');
  // Find or create the mutexes.
  bip::shared_memory_object shm;
  try {
    shm = bip::shared_memory_object(bip::open_only, mutex_name.c_str(), bip::read_write);
    mutex_region = bip::mapped_region(shm, bip::read_write);
  } catch(bip::interprocess_exception &ex){
    if (ex.get_error_code() == 7) { // Need to create the region
      bip::shared_memory_object::remove(mutex_name.c_str());
      shm = bip::shared_memory_object(bip::create_only, mutex_name.c_str(), bip::read_write);
      shm.truncate(sizeof (upgradable_mutex_type));
      mutex_region = bip::mapped_region(shm, bip::read_write);
      new (mutex_region.get_address()) Mutexes;
    } else {
      ostringstream error_stream;
      error_stream << "Can't open mutex file: " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      return;
    }      
  }

  mutexes = static_cast<Mutexes *>(mutex_region.get_address());
  
  // Trial lock of rw mutex
  try {
    LOCKINFO("WRITE 3");
    BOOST_SCOPE_EXIT(mutex_name) {
      UNLOCKINFO("WRITE 3");
    } BOOST_SCOPE_EXIT_END;
    
    bip::scoped_lock<upgradable_mutex_type> lock(mutexes->rw_mutex, boost::get_system_time() + boost::posix_time::seconds(1));
    if (lock == 0) { // Didn't grab. May be messed up.
      new (mutex_region.get_address()) Mutexes;
      LOCKINFO("WRITE 3 MESSED UP");
    }      
  } catch (bip::lock_exception &ex) {
    if (ex.get_error_code() == 15) { // Need to init the lock area
      new (mutex_region.get_address()) Mutexes;
    } else {
      ostringstream error_stream;
      error_stream << "Bad shared mutex region: " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      return;
    }
  }  
}

NAN_METHOD(SharedMapControl::Open) {
  v8::Local<v8::Object> thisObject;
  if (!info.IsConstructCall()) {
    const int argc = 5;
    v8::Local<v8::Value> argv[argc] = {info[0], info[1], info[2], info[3], info[4]};
    v8::Local<v8::Function> cons = Nan::New(constructor());
    auto new_instance = Nan::NewInstance(cons, argc, argv);
    if (!new_instance.IsEmpty()) {
      info.GetReturnValue().Set(new_instance.ToLocalChecked());
    }
    return;
  }
  
  Nan::Utf8String filename(info[0]->ToString());
  Nan::Utf8String mode(info[1]->ToString());
  size_t initial_file_size = ((int)info[2]->Int32Value()) * 1024;
  size_t max_file_size = ((int)info[3]->Int32Value()) * 1024;
  size_t initial_bucket_count = (int)info[4]->Int32Value();
  if (initial_file_size == 0) {
    initial_file_size = DEFAULT_FILE_SIZE;
  }
  // Don't open it too small.
  if (initial_file_size < MINIMUM_FILE_SIZE) {
    initial_file_size = MINIMUM_FILE_SIZE;
    max_file_size = max(initial_file_size, max_file_size);
  }
  if (max_file_size == 0) {
    max_file_size = DEFAULT_MAX_SIZE;
  }
  // Default to 1024 buckets
  if (initial_bucket_count == 0) {
    initial_bucket_count = 1024;
  }
  SharedMap *d = new SharedMap();
  d->setFilename(*filename);
  d->readonly = string(*mode) == "ro";
  d->writeonly = string(*mode) == "wo";
  d->reify_mutexes(); // Get these set up before proceeding.
  d->max_file_size = max_file_size;
  d->initial_bucket_count = initial_bucket_count;
  
  struct stat buf;

  // So we don't try to double-create the file.
  bip::scoped_lock<upgradable_mutex_type> lock(d->mutexes->rw_mutex);
  int s = stat(*filename, &buf);
  if (s == 0) {
    if (!S_ISREG(buf.st_mode)) {
      ostringstream error_stream;
      error_stream << *filename << " is not a regular file.";
      Nan::ThrowError(error_stream.str().c_str());
      return;
    }
  } else { // File doesn't exist
    if (d->readonly) {
      ostringstream error_stream;
      error_stream << *filename << " does not exist, cannot open read-only.";
      Nan::ThrowError(error_stream.str().c_str());
      return;
    }      
  }
  // Does something else have this held in WO?
  if (!d->mutexes->wo_mutex.timed_lock_sharable(boost::get_system_time() + boost::posix_time::seconds(1))) {
    Nan::ThrowError("Cannot open, another process has this open write-only.");
    return;
  }
  if (d->writeonly) { // Hold onto the WO lock exclusively.
    if (!d->mutexes->wo_mutex.timed_unlock_upgradable_and_lock(boost::get_system_time() + boost::posix_time::seconds(1))) {
      Nan::ThrowError("Cannot lock for write-only, another process has this file open.");
      return;
    }
  }
  
  SharedMapControl *c = new SharedMapControl(d);

  try {
    d->map_seg = new bip::managed_mapped_file(bip::open_or_create, string(*filename).c_str(), initial_file_size);
    d->property_map = d->map_seg->find_or_construct<PropertyHash>("properties")
      (initial_bucket_count, hasher(), s_equal_to(), d->map_seg->get_segment_manager());
    d->map_seg->flush();
  } catch(bip::interprocess_exception &ex){
    ostringstream error_stream;
    error_stream << "Can't open file " << *filename << ": " << ex.what();
    Nan::ThrowError(error_stream.str().c_str());
    return;
  }
 
  c->Wrap(info.This());
  auto map_obj = SharedMap::NewInstance();
    
  d->Wrap(map_obj);
  auto ret_obj = Nan::New<v8::Object>();
  Nan::Set(ret_obj, Nan::New("control").ToLocalChecked(), info.This());
  Nan::Set(ret_obj, Nan::New("obj").ToLocalChecked(), map_obj);
  info.GetReturnValue().Set(ret_obj);
}

void SharedMap::setFilename(string fn_string) {
  file_name = fn_string;
}

bool SharedMap::grow(size_t size) {
  // Can ONLY grow in write-only mode!
  if (!writeonly) {
    Nan::ThrowError("File needs to be larger but can only be resized in write-only mode.");
    return false;
  }
  if (size < 100)
    size = 100;
  auto file_size = map_seg->get_size();
  file_size += size;
  if (file_size > max_file_size) {
    Nan::ThrowError("File grew too large.");
    return false;
  }
  // put these guys into a utility function
  //map_seg->get_segment_manager()->shrink_to_fit_indexes();
  map_seg->flush();
  delete map_seg;
  //bip::managed_mapped_file::shrink_to_fit(file_name.c_str());
  if (!bip::managed_mapped_file::grow(file_name.c_str(), size)) {
    Nan::ThrowError("Error growing file.");
    return false;
  }
  map_seg = new bip::managed_mapped_file(bip::open_only, file_name.c_str());
  property_map = map_seg->find<PropertyHash>("properties").first;
  closed = false;
  return true;
}
  
SharedMap::~SharedMap() {
  if (!closed) {
    if (writeonly) {
      mutexes->wo_mutex.unlock_upgradable();
    } else {
      mutexes->wo_mutex.unlock_sharable();
    }
  }
}
  
struct CloseWorker : public Nan::AsyncWorker {
  SharedMap *map;
  CloseWorker(Nan::Callback *callback, SharedMap *map) : AsyncWorker(callback), map(map) {}
  virtual void Execute() { // May run in a separate thread
     if (map->writeonly) {
      bip::managed_mapped_file::shrink_to_fit(map->file_name.c_str());
    }
    map->map_seg->flush();
    delete map->map_seg;
    map->map_seg = NULL;
    if (map->writeonly) {
      map->mutexes->wo_mutex.unlock();
    }
  }
};

NAN_METHOD(SharedMapControl::Close) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  auto callback = new Nan::Callback(info[0].As<v8::Function>());
  auto closer = new CloseWorker(callback, self->map);

  if (info[0]->IsFunction()) { // Close asynchronously
    if (self->map->closed) {
      v8::Local<v8::Value> argv[1] = {Nan::Error("Attempted to close a closed object.")};
      callback->Call(1, argv);
      return;
    }
    AsyncQueueWorker(closer);
  } else {
    if (self->map->closed) {
      Nan::ThrowError("Attempted to close a closed object.");
      return;
    }
    closer->Execute();
  }  
  self->map->closed = true;
}

NAN_METHOD(SharedMapControl::isClosed) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  info.GetReturnValue().Set(self->map->closed);
}

NAN_METHOD(SharedMapControl::isOpen) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  info.GetReturnValue().Set(!self->map->closed);
}

NAN_METHOD(SharedMapControl::writeUnlock) {
  auto self = unwrap(info[0].As<v8::Object>());
  if (!self)
    return;
  self->map->inWriteLock = false;
  self->map->mutexes->rw_mutex.unlock();
  UNLOCKINFO("WRITE 5");
}

NAN_METHOD(SharedMapControl::writeLock) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  auto callback = new Nan::Callback(info[0].As<v8::Function>());
  auto func = Nan::New<v8::Function>(writeUnlock);
  // Bind function to this object
  auto bind = Nan::Get(func, Nan::New("bind").ToLocalChecked()).ToLocalChecked();
  v8::Local<v8::Value> argvl[2] = {func, info.This()};
  auto boundFunc = Nan::Call(bind.As<v8::Function>(), func, 2, argvl).ToLocalChecked();
  // Send it as callback.
  v8::Local<v8::Value> argv[1] = {boundFunc};

  self->map->mutexes->rw_mutex.lock();
  self->map->inWriteLock = true;
  LOCKINFO("WRITE 5");
  callback->Call(1, argv);
}

v8::Local<v8::Function> SharedMapControl::init_methods(v8::Local<v8::FunctionTemplate> f_tpl) {
  Nan::SetPrototypeMethod(f_tpl, "close", Close);
  Nan::SetPrototypeMethod(f_tpl, "isClosed", isClosed);
  Nan::SetPrototypeMethod(f_tpl, "isOpen", isOpen);
  Nan::SetPrototypeMethod(f_tpl, "writeLock", writeLock);
  Nan::SetPrototypeMethod(f_tpl, "remove_shared_mutex", remove_shared_mutex);
  Nan::SetPrototypeMethod(f_tpl, "get_free_memory", get_free_memory);
  Nan::SetPrototypeMethod(f_tpl, "get_size", get_size);
  Nan::SetPrototypeMethod(f_tpl, "bucket_count", bucket_count);
  Nan::SetPrototypeMethod(f_tpl, "max_bucket_count", max_bucket_count);
  Nan::SetPrototypeMethod(f_tpl, "load_factor", load_factor);
  Nan::SetPrototypeMethod(f_tpl, "max_load_factor", max_load_factor);
  f_tpl->InstanceTemplate()->SetInternalFieldCount(1);
  auto fun = Nan::GetFunction(f_tpl).ToLocalChecked();
  constructor().Reset(fun);
  return fun;
}

void SharedMapControl::Init(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE exports, ADDON_REGISTER_FUNCTION_ARGS2_TYPE module) {
  SharedMap::Init(exports);
  // The mmap opener class
  v8::Local<v8::FunctionTemplate> tmpl = Nan::New<v8::FunctionTemplate>(Open);
  tmpl->SetClassName(Nan::New("MmapObject").ToLocalChecked());
  auto open_fun = init_methods(tmpl);
  Nan::Set(module.As<v8::Object>(), Nan::New("exports").ToLocalChecked(), open_fun);
}

NODE_MODULE(mmap_object, SharedMapControl::Init)

// Todo: look for self->map-> refs and see if they can be
// encapsulated in the SharedMap directly.

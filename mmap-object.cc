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
#include "cell.hpp"
#include "aho_corasick.hpp"

#include "common.hpp"

#define SHARDS 64

#define LOCKINFO(lock) // cout << ::getpid() << " LOCK " << lock << endl
#define UNLOCKINFO(lock) // cout << ::getpid() << " UNLOCK " << lock << endl

#if BOOST_VERSION < 105500
  #pragma message("Found boost version " BOOST_PP_STRINGIZE(BOOST_LIB_VERSION))
  #error mmap-object needs at least version 1_55 to maintain compatibility.
#endif

#define MINIMUM_FILE_SIZE 10240  // Minimum necessary to handle an mmap'd unordered_map on all platforms.
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

typedef bip::basic_string<char, char_traits<char>> char_string;

// This changes whenever fields are added/changed in Cell
#define FILEVERSION 1
// Also allow version 0 for now. Revisit once FILEVERSION goes to 2.
#define ALSOOK 0

#define CHECK_VERSION(obj)                                              \
  if (obj->version != FILEVERSION && obj->version != ALSOOK) {          \
    ostringstream error_stream;                                         \
    error_stream << "File " << *filename << " is format version " << obj->version; \
    error_stream << " (version " << FILEVERSION << " is expected)";     \
    Nan::ThrowError(error_stream.str().c_str());                        \
    return;                                                             \
  }

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
  upgradable_mutex_type global_mutex;
  upgradable_mutex_type rw_mutex[SHARDS];
  upgradable_mutex_type wo_mutex;
};  

class SharedMap : public Nan::ObjectWrap {
  SharedMap(string file_name, size_t max_file_size) :
    file_name(file_name), max_file_size(max_file_size),
    readonly(false), closed(true), inGlobalLock(false) {}
  SharedMap(string file_name) :
    file_name(file_name), readonly(false), closed(true), inGlobalLock(false) {}

  friend class SharedMapControl;
  friend struct CloseWorker;
public:
  SharedMap() : readonly(false), writeonly(false), closed(false), inGlobalLock(false) {}
  virtual ~SharedMap();
  bool reify_mutexes(uint64_t base_address);
  bool allocate_mutex_memory(uint64_t base_address, string mutex_name, bool create_only);
  static NAN_MODULE_INIT(Init) {
    auto tpl = Nan::New<v8::FunctionTemplate>();
    auto inst = tpl->InstanceTemplate();
    inst->SetInternalFieldCount(1);
    Nan::SetNamedPropertyHandler(inst, PropGetter, PropSetter, PropQuery, PropDeleter, PropEnumerator,
                                 Nan::New<v8::String>("prototype").ToLocalChecked());
    Nan::SetNamedPropertyHandler(inst, PropGetter, PropSetter, PropQuery, PropDeleter, PropEnumerator,
                                 Nan::New<v8::String>("instance").ToLocalChecked());
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
  size_t initial_bucket_count;
  uint32_t version;
  bool readonly;
  bool writeonly;
  bool closed;
  PropertyHash::iterator iter;
  size_t itershard; // Iterating this shard
  bool inGlobalLock;
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
  static NAN_METHOD(next);
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
  static NAN_METHOD(fileFormatVersion);
  static NAN_METHOD(remove_shared_mutex);
  static v8::Local<v8::Function> init_methods(v8::Local<v8::FunctionTemplate> f_tpl);
  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }
  friend struct CloseWorker;
};

aho_corasick::trie methodTrie;

bool isMethod(string name) {
  return methodTrie.contains(name);
}

void buildMethods() {
  string methods[] = {
    "bucket_count",
    "close",
    "get_free_memory",
    "get_size",
    "isClosed",
    "isOpen",
    "load_factor",
    "max_bucket_count",
    "max_load_factor",
    "propertyIsEnumerable",
    "toString",
    "fileFormatVersion",
    "valueOf",
    "remove_shared_mutex"
  };

  for (const string &method : methods) {
    methodTrie.insert(method);
  }
}

SharedMapControl* SharedMapControl::unwrap(v8::Local<v8::Object> thisObj) {
  if (thisObj->InternalFieldCount() != 1 || thisObj->IsUndefined()) {
    Nan::ThrowError("Not actually an mmap control object!");
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

#define HASH(prop)                              \
  boost::hash<std::string> string_hash;         \
  std::size_t h = string_hash(*prop)%SHARDS

#define BASE_PROPERTY_MAP                                               \
  PropertyHash *property_map =                                          \
    self->map_seg->find_or_construct<PropertyHash>("properties")[SHARDS] \
    (self->initial_bucket_count, hasher(), s_equal_to(),                \
    self->map_seg->get_segment_manager());                              \
  if (property_map == NULL) {                                           \
    ostringstream error_stream;                                         \
    error_stream << "File " << self->file_name                          \
                 << " appears to be corrupt (2).";                      \
    Nan::ThrowError(error_stream.str().c_str());                        \
    return;                                                             \
  }

#define PROPERTY_MAP(prop)                      \
  HASH(prop);                                   \
  BASE_PROPERTY_MAP                             \
  property_map += h

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
  
  size_t data_length = sizeof(Cell) + Cell::ValueLength(value, info);
  v8::String::Utf8Value prop UTF8VALUE(property);

  bip::scoped_lock<upgradable_mutex_type> lock;
  bip::sharable_lock<upgradable_mutex_type> glock;
  HASH(prop);

  if (!self->inGlobalLock) {
    LOCKINFO("RW 1");
    bip::scoped_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex[h]);
    lock.swap(lock_);
    bip::sharable_lock<upgradable_mutex_type> glock_(self->mutexes->global_mutex);
    glock.swap(glock_);
    BOOST_SCOPE_EXIT(self) {
      UNLOCKINFO("WRITE 1");
    } BOOST_SCOPE_EXIT_END;
  }

  try {
    while(true) {
      try {
        BASE_PROPERTY_MAP;
        property_map += h;
        unique_ptr<Cell> c;
        Cell::SetValue(value, self->map_seg, c, info);
        char_allocator allocer(self->map_seg->get_segment_manager());
        unique_ptr<shared_string> string_key(new shared_string(string(*prop).c_str(), allocer));
        auto pair = property_map->insert({ *string_key, *c }); // ALLOC
        if (!pair.second) {
          property_map->erase(*string_key);
          property_map->insert({ *string_key, *c });
        }
        break;
      } catch (bip::lock_exception &ex) {
        ostringstream error_stream;
        error_stream << "Lock exception: " << ex.what();
        Nan::ThrowError(error_stream.str().c_str());
        return;
      } catch(length_error) {
        auto grow_size = self->map_seg->get_size() * 2;
        if (grow_size < data_length)
          grow_size = data_length;
        if (!self->grow(grow_size)) {
          return;
        }
      } catch(bip::bad_alloc) {
        auto grow_size = self->map_seg->get_size() * 2;
        if (grow_size < data_length)
          grow_size = data_length;
        if (!self->grow(grow_size)) {
          return;
        }
      }
    }
  } catch(FileTooLarge) {
    Nan::ThrowError("File grew too large.");
  }
  info.GetReturnValue().Set(value);
}

#define STRINGINDEX                                             \
  ostringstream ss;                                             \
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

NAN_METHOD(SharedMap::next) {
  // Always return an object
  auto obj = Nan::New<v8::Object>();
  info.GetReturnValue().Set(obj);
  auto self = unwrap(info.Data().As<v8::Object>());

  BASE_PROPERTY_MAP;
  property_map += self->itershard; // Get on the right shard

  // Determine if we're at the end of this shard
  while (self->iter == property_map->end()) {
    // Have we gone through all shards?
    if (++self->itershard == SHARDS) {
      Nan::Set(obj, Nan::New<v8::String>("done").ToLocalChecked(), Nan::True());
      return;
    }
    // Nope, point at the next shard.
    property_map++;
    self->iter = property_map->begin();
  }

  // Iterate and return an array of [key, value] for this step.
  auto arr = Nan::New<v8::Array>();
  arr->Set(0, Nan::New<v8::String>(self->iter->first.c_str()).ToLocalChecked()); // key

  Cell *c = &self->iter->second; // value
  arr->Set(1, c->GetValue());

  // Per iteration protocol, the value property of the returned object
  // holds the data for this iteration.
  Nan::Set(obj, Nan::New<v8::String>("value").ToLocalChecked(), arr);
  self->iter++;
}

NAN_PROPERTY_GETTER(SharedMap::PropGetter) {
  v8::String::Utf8Value data UTF8VALUE(info.Data());
  v8::String::Utf8Value src UTF8VALUE(property);

  if (!property->IsNull() && !property->IsSymbol() && methodTrie.contains(string(*src))) {
    return;
  }
  auto self = unwrap(info.This());
  if (property->IsSymbol()) {
    // Handle iteration
    if (Nan::Equals(property, v8::Symbol::GetIterator(info.GetIsolate())).FromJust()) {
      self->itershard = 0; // Start at the first shard
      BASE_PROPERTY_MAP;
      self->iter = property_map->begin(); // Reset the iterator
      auto iter_template = Nan::New<v8::FunctionTemplate>();
      Nan::SetCallHandler(iter_template, [](const Nan::FunctionCallbackInfo<v8::Value> &info) {
          auto next_template = Nan::New<v8::FunctionTemplate>();
          Nan::SetCallHandler(next_template, next, info.Data());
          auto obj = Nan::New<v8::Object>();
          Nan::Set(obj, Nan::New<v8::String>("next").ToLocalChecked(),
                   next_template->GetFunction());
          info.GetReturnValue().Set(obj);
        }, info.This());
      info.GetReturnValue().Set(iter_template->GetFunction());
    }
    // Otherwise don't return anything on symbol accesses
    return;
  }
  if (string(*data) == "prototype") {
    return;
  }
  if (self->closed) {
    Nan::ThrowError("Cannot read from closed object.");
    return;
  }

  PROPERTY_MAP(src);

  bip::sharable_lock<upgradable_mutex_type> lock, glock;
  if (!self->inGlobalLock) {
    LOCKINFO("RW SHARE 1");
    bip::sharable_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex[h]);
    lock.swap(lock_);
    bip::sharable_lock<upgradable_mutex_type> glock_(self->mutexes->global_mutex);
    glock.swap(glock_);
    BOOST_SCOPE_EXIT(self, h) {
      UNLOCKINFO("RW SHARE 1");
    } BOOST_SCOPE_EXIT_END;
  }

  // If the map doesn't have it, let v8 continue the search.
  auto pair = property_map->find<char_string, hasher, s_equal_to>
    (*src, hasher(), s_equal_to());
  if (pair == property_map->end())
    return;

  Cell *c = &pair->second;
  info.GetReturnValue().Set(c->GetValue());
}

NAN_PROPERTY_QUERY(SharedMap::PropQuery) {
  v8::String::Utf8Value src UTF8VALUE(property);

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
  
  v8::String::Utf8Value prop UTF8VALUE(property);
  PROPERTY_MAP(prop);
  
  bip::scoped_lock<upgradable_mutex_type> lock;
  bip::sharable_lock<upgradable_mutex_type> glock;
  if (!self->inGlobalLock) {
    LOCKINFO("RW 2");
    bip::scoped_lock<upgradable_mutex_type> lock_(self->mutexes->rw_mutex[h]);
    lock.swap(lock_);
    LOCKINFO("RW 2 OBTAINED");
    BOOST_SCOPE_EXIT(self) {
      UNLOCKINFO("RW 2 RELEASED");
    } BOOST_SCOPE_EXIT_END;
  }

  shared_string *string_key;
  char_allocator allocer(self->map_seg->get_segment_manager());
  string_key = new shared_string(string(*prop).c_str(), allocer);
  property_map->erase(*string_key);
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

  bip::scoped_lock<upgradable_mutex_type> lock;
  bip::sharable_lock<upgradable_mutex_type> glock;
  if (!self->inGlobalLock) {
    LOCKINFO("RW 4");
    bip::scoped_lock<upgradable_mutex_type> lock_(self->mutexes->global_mutex);
    lock.swap(lock_);
    BOOST_SCOPE_EXIT(self) {
      UNLOCKINFO("RW 4");
    } BOOST_SCOPE_EXIT_END;
  }


  int i = 0;
  PropertyHash *property_map1 = self->map_seg->find_or_construct<PropertyHash>("properties")[SHARDS] \
    (self->initial_bucket_count, hasher(), s_equal_to(), self->map_seg->get_segment_manager()); \
  // Go through all the shards
  for(int s = 0; s < SHARDS; s++) {
    auto property_map = property_map1+s;
    for (auto it = property_map->begin(); it != property_map->end(); ++it) {
	  arr->Set(i++, Nan::New<v8::String>(it->first.c_str()).ToLocalChecked());
    }
  }
  info.GetReturnValue().Set(arr);
  LOCKINFO("RW 7 RELEASED");
}

#define INFO_METHOD(name, type) NAN_METHOD(SharedMapControl::name) { \
    auto self = unwrap(info.This());                                 \
    if (!self) return;                                               \
    info.GetReturnValue().Set((type)self->map->map_seg->name());     \
  }

INFO_METHOD(get_free_memory, uint32_t);
INFO_METHOD(get_size, uint32_t);

NAN_METHOD(SharedMapControl::fileFormatVersion) {
  auto self = Nan::ObjectWrap::Unwrap<SharedMapControl>(info.This());
  info.GetReturnValue().Set((uint32_t)self->map->version);
}

NAN_METHOD(SharedMapControl::remove_shared_mutex) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  string mutex_name(self->map->file_name);
  replace( mutex_name.begin(), mutex_name.end(), '/', '-');
  bip::shared_memory_object::remove(mutex_name.c_str());
}

#define DEFAULT_BASE 0x400000000000

bool SharedMap::allocate_mutex_memory(uint64_t base_address, string mutex_name, bool create_only) {
  bip::shared_memory_object shm;
  if (create_only) {
    shm = bip::shared_memory_object(bip::create_only, mutex_name.c_str(), bip::read_write);
  } else {
    shm = bip::shared_memory_object(bip::open_only, mutex_name.c_str(), bip::read_write);
  }
  
  shm.truncate(sizeof(Mutexes));
#ifdef __APPLE__
  try {
    mutex_region = bip::mapped_region(shm, bip::read_write, 0, sizeof(Mutexes), (void*)base_address);
  } catch(bip::interprocess_exception &ex){
    ostringstream error_stream;
    error_stream << "mmap failure: " << ex.what();
    error_stream << " -- You may have to supply the base_address value to the mmap_object call";
    Nan::ThrowError(error_stream.str().c_str());
    return false;
  }
#else
  mutex_region = bip::mapped_region(shm, bip::read_write);
#endif
  return true;
}

bool SharedMap::reify_mutexes(uint64_t base_address) {
  string mutex_name(file_name);
  replace(mutex_name.begin(), mutex_name.end(), '/', '-');
  // Find or create the mutexes.
  bip::shared_memory_object shm;
  if (base_address == 0) {
    base_address = DEFAULT_BASE;
  }
  try {
    if (!allocate_mutex_memory(base_address, mutex_name, false)) return false;
  } catch(bip::interprocess_exception &ex){
    if (ex.get_error_code() == 7) { // Need to create the region
      bip::shared_memory_object::remove(mutex_name.c_str());
      if (!allocate_mutex_memory(base_address, mutex_name, true)) return false;
      new (mutex_region.get_address()) Mutexes;
    } else {
      ostringstream error_stream;
      error_stream << "Can't open mutex file: " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      return false;
    }      
  }

  mutexes = static_cast<Mutexes *>(mutex_region.get_address());
  
  // Trial lock of global mutex
  try {
    LOCKINFO("RW 3");
    BOOST_SCOPE_EXIT(mutex_name) {
      UNLOCKINFO("RW 3");
    } BOOST_SCOPE_EXIT_END;

    bip::scoped_lock<upgradable_mutex_type> lock(mutexes->global_mutex, boost::get_system_time() + boost::posix_time::seconds(1));
    if (lock == 0) { // Didn't grab. May be messed up.
      new (mutex_region.get_address()) Mutexes;
      LOCKINFO("RW 3 MESSED UP");
    } else {
      LOCKINFO("RW 3 OBTAINED");
    }
  } catch (bip::lock_exception &ex) {
    if (ex.get_error_code() == 15) { // Need to init the lock area
      new (mutex_region.get_address()) Mutexes;
    } else {
      ostringstream error_stream;
      error_stream << "Bad shared mutex region: " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      return false;
    }
  }
  return true;
}

NAN_METHOD(SharedMapControl::Open) {
  v8::Local<v8::Object> thisObject;
  if (!info.IsConstructCall()) {
    const int argc = info.Length();
    v8::Local<v8::Value> argv[6] = {info[0], info[1], info[2], info[3], info[4], info[5]};
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
  uint64_t map_address = 0;
  if (info.Length() > 5) { // Grab base address
    map_address = info[5]->IntegerValue();
  }
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
  SharedMap *d = new SharedMap(*filename);
  d->readonly = string(*mode) == "ro";
  d->writeonly = string(*mode) == "wo";
  if (!d->reify_mutexes(map_address)) { // Get these set up before proceeding.
    return; // Reify failed, exception should follow.
  }
  d->max_file_size = max_file_size;
  d->initial_bucket_count = initial_bucket_count;
  struct stat buf;
  { // Mutex scope
    bip::sharable_lock<upgradable_mutex_type> lock(d->mutexes->global_mutex);
    int stat_result = stat(*filename, &buf);
    if (stat_result == 0) {
      if (!S_ISREG(buf.st_mode)) {
        ostringstream error_stream;
        error_stream << *filename << " is not a regular file.";
        Nan::ThrowError(error_stream.str().c_str());
        goto ABORT;
      }
      if (buf.st_size == 0) {
        ostringstream error_stream;
        error_stream << *filename << " is an empty file.";
        Nan::ThrowError(error_stream.str().c_str());
        goto ABORT;
      }      
    } else { // File doesn't exist
      if (d->readonly) {
        ostringstream error_stream;
        error_stream << *filename << " does not exist, cannot open read-only.";
        Nan::ThrowError(error_stream.str().c_str());
        goto ABORT;
      }      
    }
    // Does something else have this held in WO?
    LOCKINFO("OPEN WO SHARED");
    if (!d->mutexes->wo_mutex.timed_lock_sharable(boost::get_system_time() + boost::posix_time::seconds(1))) {
      Nan::ThrowError("Cannot open, another process has this open write-only.");
      goto ABORT;
    }
    if (d->writeonly) { // Hold onto the WO lock exclusively.
      LOCKINFO("OPEN WO UPGRADED");
      if (!d->mutexes->wo_mutex.timed_unlock_upgradable_and_lock(boost::get_system_time() + boost::posix_time::seconds(1))) {
        Nan::ThrowError("Cannot lock for write-only, another process has this file open.");
        goto ABORT;
      }
    }
  
    SharedMapControl *c = new SharedMapControl(d);
    try {
      d->map_seg = new bip::managed_mapped_file(bip::open_or_create, string(*filename).c_str(), initial_file_size);
      if (stat_result == 0 && d->map_seg->get_size() != (unsigned long)buf.st_size) {
        ostringstream error_stream;
        error_stream << "File " << *filename << " appears to be corrupt (1).";
        Nan::ThrowError(error_stream.str().c_str());
        goto ABORT;
      }
      d->version = FILEVERSION;
      auto vers = d->map_seg->find_or_construct<uint32_t>("version")(FILEVERSION);
      if (vers == NULL) {
        d->version = 0; // No version but should be compatible with V1.
      } else {
        d->version = *vers;
      }
      CHECK_VERSION(d);
      d->map_seg->flush();
    } catch(bip::interprocess_exception &ex){
      ostringstream error_stream;
      error_stream << "Can't open file " << *filename << ": " << ex.what();
      Nan::ThrowError(error_stream.str().c_str());
      goto ABORT;
    }
    c->Wrap(info.This());
    auto map_obj = SharedMap::NewInstance();
    
    d->closed = false;
    d->Wrap(map_obj);
    auto ret_obj = Nan::New<v8::Object>();
    Nan::Set(ret_obj, Nan::New("control").ToLocalChecked(), info.This());
    Nan::Set(ret_obj, Nan::New("obj").ToLocalChecked(), map_obj);
    info.GetReturnValue().Set(ret_obj);
    return;
  }
 ABORT:
  // Failure, ditch the mutexes.
  d->mutex_region.~mapped_region();
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
    throw FileTooLarge();
  }
  map_seg->flush();
  delete map_seg;
  if (!bip::managed_mapped_file::grow(file_name.c_str(), size)) {
    Nan::ThrowError("Error growing file.");
    return false;
  }
  map_seg = new bip::managed_mapped_file(bip::open_only, file_name.c_str());
  closed = false;
  return true;
}
  
SharedMap::~SharedMap() {
  if (!closed) {
    if (writeonly) {
      UNLOCKINFO("OPEN WO UPGRADED");
      mutexes->wo_mutex.unlock_upgradable();
    } else {
      LOCKINFO("OPEN WO SHARED");
      mutexes->wo_mutex.unlock_sharable();
    }
  }
}
  
struct CloseWorker : public Nan::AsyncWorker {
  bip::managed_mapped_file *map_seg;
  string file_name;
  upgradable_mutex_type *wo_mutex;
  bool writeonly;
  bip::mapped_region *mutex_region;
  CloseWorker(Nan::Callback *callback, bip::managed_mapped_file *map_seg,
              string file_name, upgradable_mutex_type *wo_mutex,
              bool writeonly, bip::mapped_region *mutex_region) :
    AsyncWorker(callback), map_seg(map_seg), file_name(file_name),
    wo_mutex(wo_mutex), writeonly(writeonly), mutex_region(mutex_region) {}
  virtual void Execute() { // May run in a separate thread
    {
      LOCKINFO("CLOSE");
      if (writeonly) {
        bip::managed_mapped_file::shrink_to_fit(file_name.c_str());
      }
      map_seg->flush();
      delete map_seg;
      if (writeonly) {
        UNLOCKINFO("OPEN WO UPGRADED");
        wo_mutex->unlock();
      } else {
        UNLOCKINFO("OPEN WO SHARED");
        wo_mutex->unlock_sharable();
      }
    }
    mutex_region->~mapped_region();
  }
};

NAN_METHOD(SharedMapControl::Close) {
  auto self = unwrap(info.This());
  if (!self)
    return;

  auto callback = new Nan::Callback(info[0].As<v8::Function>());
  auto closer = new CloseWorker(callback, self->map->map_seg, self->map->file_name,
                                &self->map->mutexes->wo_mutex, self->map->writeonly,
                                &self->map->mutex_region);

  if (info[0]->IsFunction()) { // Close asynchronously
    if (self->map->closed) {
      v8::Local<v8::Value> argv[1] = {Nan::Error("Attempted to close a closed object.")};
      Nan::AsyncResource resource("mmap-object:Close");
      callback->Call(1, argv, &resource);
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
  self->map->map_seg = NULL;
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
  auto self = unwrap(info.This());
  if (!self)
    return;
  self->map->inGlobalLock = false;
  self->map->mutexes->global_mutex.unlock();
  UNLOCKINFO("WR 5");
}

NAN_METHOD(SharedMapControl::writeLock) {
  auto self = unwrap(info.This());
  if (!self)
    return;
  auto func = Nan::New<v8::Function>(writeUnlock);
  // Bind the unlock function to this object
  auto bind = Nan::Get(func, Nan::New("bind").ToLocalChecked()).ToLocalChecked();
  v8::Local<v8::Value> argvl[1] = {info.This()};
  auto boundFunc = Nan::Call(bind.As<v8::Function>(), func, 1, argvl).ToLocalChecked();
  // Send it to the given callback as a callback.
  v8::Local<v8::Value> argv[1] = {boundFunc};
  LOCKINFO("RW 5");
  self->map->mutexes->global_mutex.lock();
  self->map->inGlobalLock = true;
  LOCKINFO("RW 5 OBTAINED");
  Nan::AsyncResource resource("mmap-object:writeLock");
  Nan::Callback(info[0].As<v8::Function>()).Call(1, argv, &resource);
}

v8::Local<v8::Function> SharedMapControl::init_methods(v8::Local<v8::FunctionTemplate> f_tpl) {
  Nan::SetPrototypeMethod(f_tpl, "close", Close);
  Nan::SetPrototypeMethod(f_tpl, "isClosed", isClosed);
  Nan::SetPrototypeMethod(f_tpl, "isOpen", isOpen);
  Nan::SetPrototypeMethod(f_tpl, "writeLock", writeLock);
  Nan::SetPrototypeMethod(f_tpl, "remove_shared_mutex", remove_shared_mutex);
  Nan::SetPrototypeMethod(f_tpl, "get_free_memory", get_free_memory);
  Nan::SetPrototypeMethod(f_tpl, "get_size", get_size);
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

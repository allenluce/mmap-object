// To handle Node 10's deprecated non-isolate v8::String::Utf8Value version
#define NODE_10_0_MODULE_VERSION 64
#if NODE_MODULE_VERSION >= NODE_10_0_MODULE_VERSION
  #define UTF8VALUE(value) (info.GetIsolate(), value)
#else
  #define UTF8VALUE(value) (value)
#endif

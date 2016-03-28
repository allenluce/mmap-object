# Shared Memory Objects

[![Build Status](https://travis-ci.org/allenluce/mmap-object.svg?branch=master)](https://travis-ci.org/allenluce/mmap-object)

Map Javascript objects into shared memory for simultaneous access by
different Node processes running on the same machine.  The shared
memory is mmap'd from an underlying file.  Object access is handled by
Boost's unordered map class.  Two modes are currently supported:

## Write-only Mode

This is where a single process creates a new file which is mapped to a
Javascript object.  Setting properties on this object writes those
properties to the file.  You can read from the object within this mode
but sharing an object in write-only mode with other processes is
certain to result in crashes.

## Read-only mode

This opens an existing file in for reading.  Multiple processes can
open this file on a shared basis: only one copy will remain in memory
and is shared among the processes.

## Node Module

### Requirements

Binaries are provided for OSX, Linux and various node versions (check
the releases page to see which). If a binary is not provided for your
platform, you will need Boost and and a C++11 compliant compiler (like
GCC 4.8 or better).

#### MSVS build prerequisites

You need at least Visual Studio Community 2015 edition (C++11 compliant) installed.  
Setup [Boost](http://www.boost.org/):

```
bootstrap
b2 --build-type=complete
```

Set BOOST_ROOT environment variable.

### Installation

    npm install mmap-object

## Usage

```javascript
// Write a file
const Shared = require('mmap-object')

const shared_object = new Shared.Create('filename')

shared_object['new_key'] = 'some value'
shared_object.new_property = 'some other value'

shared_object.close()


// Read a file
const Shared = require('mmap-object')

const shared_object = new Shared.Open('filename')

console.log(`My value is ${shared_object.new_key}`)


// Erase a key
shared_object['new_key'] = null;
```


## API

### new Create(path, [file_size], [initial_bucket_count], [max_file_size], [safe])

Creates a new file mapped into shared memory or opens existing one.  Returns an object that
provides access to the shared memory.  Throws an exception on error.  
Note: to modify file size or bucket count of existing file you need to delete file first.

__Arguments__

* `path` - The path of the file to create
* `file_size` - *Optional* The initial size of the file in bytes.  If
  more space is needed, the file will automatically be grown to a
  larger size.  Minimum is 500 bytes.  Defaults to 5 megabytes.
* `initial_bucket_count` - *Optional* The number of buckets to
  allocate initially.  This is passed to the underlying
  [Boost unordered_map](http://www.boost.org/doc/libs/1_55_0/doc/html/boost/unordered_map.html).
  Defaults to 1024. Set this to the number of keys you expect to write.
* `max_file_size` - *Optional* The largest the file is allowed to
  grow.  If data is added beyond this limit, an exception is thrown.
  Defaults to 5 gigabytes.
* `safe` - When true, operations on shared memory are thread-safe between processes using
  [interprocess sharable mutex](http://www.boost.org/doc/libs/1_55_0/boost/interprocess/sync/interprocess_sharable_mutex.hpp)
  (`Open` must use safe=true too to make this work)


__Example__

```js
// Create a 500K map for 300 objects.
let obj = new Create("/tmp/sharedmem", null, 500000, 300)

// Create thread-safe 500K map for 300 objects.
let obj = new Create("/tmp/sharedmem", 500000, 300, null, true)
```

### new Open(path, [safe])

Maps an existing file into shared memory.  Returns an object that
provides read-only access to the shared memory.  Throws an exception
on error.  Any number of processes can open the same file but only a
single copy will reside in memory.  uses `mmap` under the covers, so
only the part of the file that is actually accesses will be loaded.

__Arguments__

* `path` - The path of the file to open
* `safe` - When true, operations on shared memory are thread-safe between processes using
  [interprocess sharable mutex](http://www.boost.org/doc/libs/1_55_0/boost/interprocess/sync/interprocess_sharable_mutex.hpp)
  (Works when `Create` writer is using safe=true too)

__Example__

```js
// Open up that shared file
let obj = new Open("/tmp/sharedmem")

// open safe
let obj = new Open("/tmp/sharedmem", true)
```

### close()

Unmaps a previously created or opened file.  If the file was created,
`close` will first shrink the file to remove any unneeded space that
may have been allocated.

_Always close your files_ if your process isn't about to exit.
Failure to do so will result in the process hanging onto references to
the shared memory. This could eventually chew up all your core.

__Example__

```js
obj.close()
```

### Object.keys()

Get all keys of object as array.

__Example__

```js
// Open up that shared file
Object.keys(obj).forEach(function (key) {
    console.log(key + '=' + util.inspect(obj[key]));
});
```

### isOpen()

Return true if this object is currently open.

### isClosed()

Return true if this object has been closed.

### get_free_memory()

Number of bytes of free storage left in the file.

### get_size()

The size of the storage in the file, in bytes.

### bucket_count()

The number of buckets currently allocated in the underlying hash structure.

### max_bucket_count()

The maximum number of buckets that can be allocated in the underlying hash structure.

### load_factor()

The average number of elements per bucket.

### max_load_factor()

The current maximum load factor

## Unit tests

    npm test

## Limitations

[the `in` operator](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/in)
is slow. Use [`keys` method](https://developer.mozilla.org/ru/docs/Web/JavaScript/Reference/Global_Objects/Object/keys)
to iterate.

_It is strongly recommended_ to pass in the number of keys you expect
to write when creating the object with `Create`.  If you don't do
this, the object will resize once you fill it up. This can be a very
time-consuming process and can result in fragmentation within the
shared memory object and a larger final file size.

Object values may be string, number, object or array values.  Attempting to set
a different type of value results in an exception.

Symbols are not supported as properties.

Note: object values writes takes the whole object atomically, setting up shared object's properties will
not update stored value.

## Publishing a binary release

To make a new binary release:

- Edit package.json.  Increment the `version` property.
- `node-pre-gyp rebuild`
- `node-pre-gyp package`
- `node-pre-gyp-github publish`
- `npm publish`

You will need a `NODE_PRE_GYP_GITHUB_TOKEN` with `repo:status`,
`repo_deployment` and `public_repo` access to the target repo. You'll
also need write access to the npm repo.

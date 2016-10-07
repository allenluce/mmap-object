# Shared Memory Objects

[![Build Status](https://travis-ci.org/allenluce/mmap-object.svg?branch=master)](https://travis-ci.org/allenluce/mmap-object)

Super-fast file-based sharing of Javascript objects among multiple
processes.

This module maps Javascript objects into shared memory for
simultaneous access by different Node processes running on the same
machine. Shared memory is loaded via
[mmap](https://en.wikipedia.org/wiki/Mmap).  Object access is mediated
by Boost's unordered map class for speedy accesses.

Data is lazily loaded as needed so opening even a huge file takes no
time at all.

```js
const MMO = require('mmap-object')
let {control, obj} = new MMO('shared_file') // Creates the file

obj[1] = 'hey'
obj.new_property = 'what'
obj['new_key'] = 'now'
delete obj['new_key']

control.close()
```

Read it from another process:

```js
const MMO = require('mmap-object')
let {control, obj} = new MMO('shared_file') // Creates the file

for (key of obj) {
obj[1] = 'hey'
obj.new_property = 'what'
obj['new_key'] = 'now'
delete obj['new_key']

control.close()
```

## Performance mode

Read-only:
- Removes all locking. Disallows any writes into the DB.

let {control, obj} = require('mmap-object')


## Faster performance with buffers

If you use lengthy data values,
[buffers](https://nodejs.org/api/buffer.html) can speed things up
considerably. In rough benchmarking, a 300% speedup was see when
reading 20k-byte values as buffers instead of strings. For 200k-byte
values, the speedup was 2000%.

## Requirements

Binaries are provided for OSX and Linux for various node versions
(check the releases page to see which). If a binary is not provided
for your platform, you will need Boost and and a C++11 compliant
compiler (like GCC 4.8 or better) to build the module.

## Installation

    npm install mmap-object

## Usage

```javascript
// Create a file
const MMO = require('mmap-object')
const m = MMO('filename')
const shared_object = m.obj
const control = m.control

shared_object['new_key'] = 'a string value'
shared_object.new_property = Buffer.from('a buffer value, supporting Unicode™')
shared_object['useless key'] = 0

// Erase a key
delete shared_object['new_key']

// Close the object and underlying file.
control.close()

// Open a file read-only
const read_only_shared = MMO('filename', 'ro')
console.log(`My value is ${read_only_shared.obj.new_key}`)
console.log(`My other value is ${read_only_shared.obj.new_property}`)

read_only_shared.control.close()
```

## API

### MMO(path, [mode], [initial_file_size], [max_file_size], [initial_bucket_count])

Opens an existing file or creates a new file mapped into shared
memory. Returns an object that provides access to the shared
memory. Throws an exception on error.

__Arguments__

* `path` - The path of the file to create
* `mode` - 'rw' for read-write mode (the default), 'ro' for read-only
  mode, 'wo' for write-only mode (i.e. single-process access).
* `max_file_size` - *Optional* in 'wo' mode, the largest the file is
  allowed to grow in kilobytes. If data is added beyond this limit,
  an exception is thrown.  Defaults to 5 gigabytes. Ignored in 'rw'
  and 'ro' modes.
* `initial_file_size` - *Optional* On create, the initial size of the
  file in kilobytes. If more space is needed, the file will automatically
  be grown to a larger size. Minimum is 1k. Defaults to 1k.
* `initial_bucket_count` - *Optional* On create, the number of buckets
  to allocate initially. This is passed to the underlying
  [Boost unordered_map](http://www.boost.org/doc/libs/1_38_0/doc/html/boost/unordered_map.html).
  Defaults to 1024. Set this to the number of keys you expect to
  write.

__Example__

```js
// Create a 500K map for 300 objects.
const obj = new MMO('/tmp/sharedmem', 'wo', 500, 300)
```

A file can be opened read-only only if no processes have it currently
opened read-write. Once opened read-only, it can no longer be opened
on a read-write basis. Any attempts to set properties on a read-only
object will fail with an exception.

## Control

The returned `control` object has several methods for manipulating the
file and data structure:

### close()

Unmaps a previously created or opened file. If the file was opened
write-only, `close()` will first shrink the file to remove any
unneeded space that may have been allocated.

It's important to close your unused shared files in long-running
processes. Not doing so keeps shared memory from being freed.

The closing of very large objects (a few gigabytes and up) may take
some time (hundreds to thousands of milliseconds). To prevent blocking
the main thread, pass a callback to `close()`. The call to `close()`
will return immediately while the callback will be called after the
underlying `munmap()` operation completes. Any error will be given as
the first argument to the callback.

__Example__

```js
obj.close(function (err) {
  if (err) {
    console.error(`Error closing object: ${err}`)
  }
})
```

### Iteration

The [iterable
protocol](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Iteration_protocols#The_iterable_protocol)
is supported for efficient iteration over the entire contents of the
object:

```js
const Shared = require('mmap-object')
const mmo = MMO('filename')

for (let [key, value] of mmo.obj) {
    console.log(`${key} => ${value}`)
}
```

(This ES6 syntax is supported in Node 6+, for previous versions of
node a more laborious syntax is necessary.)

### isOpen()

Return true if this object is currently open.

### isClosed()

Return true if this object has been closed.

### get_free_memory()

Number of bytes of free storage left in the shared object file.

### get_size()

The size of the storage in the shared object file, in bytes.

### bucket_count()

The number of buckets currently allocated in the underlying hash structure.

### max_bucket_count()

The maximum number of buckets that can be allocated in the underlying hash structure.

### load_factor()

The average number of elements per bucket.

### max_load_factor()

The current maximum load factor.

## Read-only mode

This is a convenience/safety mode. Any writes to the object will produce an error.

## Write-only mode

This is a convenience mode for creating files that are intended to be
primarily read-only. Write-only mode is like read-write mode but with
three differences:

1. If the file is created too small initially, it is grown as
necessary (up to `max_file_size`).
2. When the file is closed, unused space is compacted to make the file smaller.
3. Only a single process can have the file open.

## Unit tests

    npm test

## Limitations

_It is strongly recommended_ to pass in the number of keys you expect
to write when initially creating the file. If you don't do this, the
object will resize as you fill it up. This can be a very
time-consuming process and can result in fragmentation within the
shared memory object and a larger final file size.

Object values may be only string, buffer, or number values. Attempting
to set a different type value results in an exception.

Symbols are not supported as properties.

## Publishing a binary release

To make a new binary release:

- Edit package.json. Increment the `version` property.
- `node-pre-gyp rebuild`
- `node-pre-gyp package`
- `node-pre-gyp-github publish`
- `npm publish`

You will need a `NODE_PRE_GYP_GITHUB_TOKEN` with `repo:status`,
`repo_deployment` and `public_repo` access to the target repo. You'll
also need write access to the npm repo.

## MSVS build prerequisites

Set up [Boost](http://www.boost.org/).

Set BOOST_ROOT environment variable.

```
bootstrap
b2 --build-type=complete
```

====

Limitations:

Inter-process locking is unstable on OSX.  You can have a single
process write the file then close it and several other processes open
it up read-only, but multiple writers won't cut it.

Shared memory mediates access to the objects so you cannot safely
share a read-write file across a mounted volume (NFS, SMB, etc).

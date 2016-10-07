'use strict'
const Aerospike = require('aerospike')
const Key = Aerospike.Key
const assert = require('assert')
const async = require('async')

const childNo = process.argv[3]
const childCount = process.argv[4]
const loopCount = process.argv[5]

const config = {
  hosts: 'localhost'
}

let client
Aerospike.connect(config, (error, cli) => {
  client = cli
  process.send('started')
})

process.on('message', function (msg) {
  const data = 'boogabooga'
  const record = {
    s: data
  }
  const meta = { ttl: 60 }
  const policy = { exists: Aerospike.policy.exists.CREATE_OR_REPLACE }

  switch (msg) {
    case 'write':
      // Do a bunch of writes based on our childNo here.
      async.timesLimit(loopCount, 200, function (i, cb) {
        var key = new Key('test', 'benchmark', `child${childNo}_${i}`)
        client.put(key, record, meta, policy, cb)
      }, function (error) {
        assert(error === null)
        process.send('wrote') // Report that we're done.
      })
      break
    case 'read':
      // Read and verify stuff that a different process wrote
      const otherChild = (childNo + 1) % childCount
      async.timesLimit(loopCount, 200, function (i, cb) {
        var key = new Key('test', 'benchmark', `child${otherChild}_${i}`)
        client.get(key, (error, record, meta) => {
          assert(error === null)
          assert(record.s === data)
          cb()
        })
      }, function (error) {
        assert(error === null)
        process.send('didread') // Report that we're done.
      })
      break
    case 'exit':
      process.exit()
      break
  }
})


'use strict'
const redis = require('redis')
const client = redis.createClient()
const assert = require('assert')

const childNo = process.argv[3]
const childCount = process.argv[4]
const loopCount = process.argv[5]

process.on('message', function (msg) {
  const data = 'boogabooga'
  switch (msg) {
    case 'write':
      // Do a bunch of writes based on our childNo here.
      const multi = client.multi()
      for (let i = 0; i < loopCount; i++) {
        let key = `child${childNo}_${i}`
        multi.set(key, data)
      }
      multi.exec(function (err, replies) {
        assert(err === null)
        process.send('wrote') // Report that we're done.
      })
      break
    case 'read':
      // Read and verify stuff that a different process wrote
      const otherChild = (childNo + 1) % childCount
      const multi_write = client.multi()
      for (let i = 0; i < loopCount; i++) {
        let key = `child${otherChild}_${i}`
        multi_write.get(key, function (err, reply) {
          assert(err === null)
          assert(reply === 'boogabooga')
        })
      }
      multi_write.exec(function (err, replies) {
        assert(err === null)
        process.send('didread') // report that we're done
      })
      break
    case 'exit':
      process.exit()
      break
  }
})

process.send('started')

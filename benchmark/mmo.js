'use strict'
const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MMO = require(mmap_obj_path)
const assert = require('assert')

const dbPath = process.argv[2]
const childNo = process.argv[3]
const childCount = process.argv[4]
const loopCount = process.argv[5]

const obj = MMO(dbPath, 'rw', 20000, 20000)

process.on('message', function (msg) {
  switch (msg) {
    case 'write':
      // Do a bunch of writes based on our childNo here.
      for (let i = 0; i < loopCount; i++) {
        obj.obj[`child${childNo}_${i}`] = 'boogabooga'
      }
      process.send('wrote') // Report that we're done.
      break
    case 'read':
      // Read the stuff that ANOTHER process wrote
      const otherChild = (childNo + 1) % childCount
      for (let i = 0; i < loopCount; i++) {
        const data = obj.obj[`child${otherChild}_${i}`]
        assert(data === 'boogabooga')
      }
      process.send('didread') // report that we're done
      break
    case 'readonly':
      {
        // Read in readonly
        const obj = MMO(dbPath, 'ro')
        const otherChild = (childNo + 1) % childCount
        // Do twice as many to keep op count in parity with rw case
        for (let i = 0; i < loopCount * 2; i++) {
          const data = obj.obj[`child${otherChild}_${i % loopCount}`]
          assert(data === 'boogabooga')
        }
        process.send('didreadonly') // report that we're done
        obj.control.close()
      }
      break
    case 'exit':
      process.exit()
      break
  }
})

process.send('started')

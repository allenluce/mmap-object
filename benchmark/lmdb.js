'use strict'
var lmdb = require('node-lmdb')
const assert = require('assert')

const dbPath = process.argv[2]
const childNo = process.argv[3]
const childCount = process.argv[4]
const loopCount = process.argv[5]
var env = new lmdb.Env()

env.open({
  path: dbPath,
  mapSize: 2*1024*1024*1024, // maximum database size
  maxDbs: 3
})

var dbi = env.openDbi({
  name: "mydb1"
})

process.on('message', function (msg) {
  switch (msg) {
    case 'write':
      // Do a bunch of writes based on our childNo here.
      var txn = env.beginTxn()
      for (let i = 0; i < loopCount; i++) {
        txn.putString(dbi, `child${childNo}_${i}`, 'boogabooga')
      }
      txn.commit()
      process.send('wrote') // Report that we're done.
      break
    case 'read':
      // Read the stuff that ANOTHER process wrote
      const otherChild = (childNo + 1) % childCount
      var txn = env.beginTxn()
      for (let i = 0; i < loopCount; i++) {
        const data = txn.getString(dbi, `child${otherChild}_${i}`)
        assert(data == 'boogabooga')
      }
      txn.commit()
      process.send('didread') // report that we're done
      break
    case 'exit':
      dbi.close()
      env.close()
      process.exit()
      break
  }
})
process.send('started')

'use strict'
const binary = require('node-pre-gyp')
const path = require('path')
const mmapObjPath = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MMO = require(mmapObjPath)

const SLOT2 = 0x710000000000

const filename = process.argv[2]

const m = MMO(filename, 'rw', 10000, 5000000, 1024, SLOT2)

process.send('started')
process.on('message', function (msg) {
  switch (msg) {
    case 'read':
      process.send(m.obj['one'])
      break
    case 'write':
      m.control.writeLock(function (cb) {
        m.obj['two'] = 'proc' + process.pid
        setTimeout(function () {
          if (m.obj['two'] === 'proc' + process.pid) {
            process.send('fourth')
            cb()
          }
        }, 20)
      })
      break
    case 'exit':
      m.control.close()
      process.exit()
      break
  }
})

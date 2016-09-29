'use strict'
const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MMO = require(mmap_obj_path)

const filename = process.argv[2]

const m = MMO(filename)

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
      process.exit()
      break
  }
})

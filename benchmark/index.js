'use strict'
const Benchmark = require('benchmark')
const suite = new Benchmark.Suite()
const temp = require('temp')
const path = require('path')
const child_process = require('child_process')
const EventEmitter = require('events')

var lmdb = require('node-lmdb')

temp.track()

const tempdir = temp.mkdirSync('node-shared')
const CHILDCOUNT = 100
const dbPaths = {}

dbPaths.lmdb = tempdir

// lmdb prep
var env = new lmdb.Env()
env.open({
  path: dbPaths.lmdb,
  maxDbs: 3
})

var dbi = env.openDbi({
  name: 'mydb1',
  create: true
})
dbi.close()
env.close()

// MMO prep
dbPaths.mmo = path.join(tempdir, 'mmofile')

// Have 10 children open and write their things.
const children = {}
const types = ['mmo', 'lmdb']
const state = {}

function sendall (which, msg) {
  children[which].forEach(function (child) {
    child.send(msg)
  })
}

const signaler = new EventEmitter()

// Child state handler
const handler = function (which) {
  return function (msg) {
    switch (msg) {
      case 'wrote':
        state[which][0]++
        if (state[which][0] === CHILDCOUNT) {
          sendall(which, 'read')
        }
        break
      case 'didread':
        if (state[which][0] !== CHILDCOUNT) {
          console.error('Got a message out of order!')
        }
        state[which][1]++
        if (state[which][1] === CHILDCOUNT) {
          signaler.emit('done')
        }
        break
      case 'didreadonly':
        state[which][0]++
        if (state[which][0] === CHILDCOUNT) {
          signaler.emit('done')
        }
        break
    }
  }
}

types.forEach(function (which) {
  children[which] = []
  for (let i = 0; i < CHILDCOUNT; i++) {
    children[which][i] = child_process.fork(`./benchmark/${which}.js`, [dbPaths[which], i, CHILDCOUNT])
    children[which][i].on('message', handler(which))
  }
})

function runchildren (which, mode) {
  return function (deferred) {
    signaler.once('done', function () {
      deferred.resolve()
    })
    state[which] = [0, 0]
    sendall(which, mode)
  }
}

suite.add('mmap-object read-write', {
  defer: true,
  fn: runchildren('mmo', 'write')
}).add('mmap-object read-only', {
  defer: true,
  fn: runchildren('mmo', 'readonly')
}).add('lmdb read-write', {
  defer: true,
  fn: runchildren('lmdb', 'write')
}).on('cycle', function (event) {
  console.log(String(event.target))
}).on('complete', function () {
  console.log('Fastest is ' + this.filter('fastest').map('name'))
  types.forEach(function (which) {
    sendall(which, 'exit')
  })
}).run()

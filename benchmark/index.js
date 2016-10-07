'use strict'
const Benchmark = require('benchmark')
const suite = new Benchmark.Suite()
const temp = require('temp')
const path = require('path')
const child_process = require('child_process')
const EventEmitter = require('events')
const async = require('async')
const assert = require('assert')
const lmdb = require('node-lmdb')

temp.track()
const tempdir = temp.mkdirSync('node-shared')

const CHILDCOUNT = 10
const LOOPCOUNT = 100

class Base {
  constructor () {
    this.state = [0, 0, 0]
    this.signaler = new EventEmitter()
  }

  setup (cb) {
    this.children = []
    this.signaler.once('ready', cb)
    for (var i = 0; i < CHILDCOUNT; i++) {
      var child = child_process.fork(`./benchmark/${this.script}.js`, [this.dbPath, i, CHILDCOUNT, LOOPCOUNT])
      child.on('message', (msg) => this.handler(msg))
      this.children.push(child)
    }
  }

  complete () {
    this.sendall('exit')
  }

  sendall (msg) {
    this.children.forEach(function (child) {
      child.send(msg)
    })
  }

  runchildren (mode) {
    return deferred => {
      this.state = [0, 0, 0]
      this.signaler.once('done', function () {
        deferred.resolve()
      })
      this.sendall(mode)
    }
  }

  handler (msg) {
    switch (msg) {
      case 'started':
        this.state[0]++
        if (this.state[0] === CHILDCOUNT) {
          this.signaler.emit('ready')
        }
        break
      case 'wrote':
        this.state[1]++
        if (this.state[1] === CHILDCOUNT) {
          this.sendall('read')
        }
        break
      case 'didread':
        if (this.state[1] !== CHILDCOUNT) {
          console.error('Got a message out of order!')
        }
        this.state[2]++
        if (this.state[2] === CHILDCOUNT) {
          this.signaler.emit('done')
        }
        break
      case 'didreadonly':
        this.state[1]++
        if (this.state[1] === CHILDCOUNT) {
          this.signaler.emit('done')
        }
        break
    }
  }
}

const benchmarks = {
  lmdb: class extends Base {
    constructor () {
      super()
      this.dbPath = tempdir
      this.script = 'lmdb'
      const env = new lmdb.Env()
      env.open({
        path: this.dbPath,
        maxDbs: 3
      })
      const dbi = env.openDbi({
        name: 'mydb1',
        create: true
      })
      dbi.close()
      env.close()
    }

    fn () {
      return this.runchildren('write')
    }
  },

  mmoReadWrite: class extends Base {
    constructor () {
      super()
      this.dbPath = path.join(tempdir, 'mmofile')
      this.script = 'mmo'
    }

    fn () {
      return this.runchildren('write')
    }
  },

  mmoReadOnly: class extends Base {
    constructor () {
      super()
      this.dbPath = path.join(tempdir, 'mmofile')
      this.script = 'mmo'
    }

    fn () {
      return this.runchildren('readonly')
    }
  },

  redis: class extends Base {
    constructor () {
      super()
      this.script = 'redis'
    }

    fn () {
      return this.runchildren('write')
    }
  },

  aerospike: class extends Base {
    constructor () {
      super()
      this.script = 'aerospike'
    }

    fn () {
      return this.runchildren('write')
    }
  }
}

//benchmarks.aerospike.only = true
// Handy for testing

let only = false

for (var b in benchmarks) {
  if (benchmarks[b].only) {
    only = true
  }
}

async.forEachOf(benchmarks, function (Case_class, case_name, cb) {
  if (Case_class.skip) {
    return cb()
  }
  if (only && !Case_class.only) {
    return cb()
  }
  const bench_case = new Case_class()
  bench_case.setup(function () {
    suite.add(case_name, {
      defer: true,
      onComplete: function (e) {
        bench_case.complete()
        e.target.hz *= CHILDCOUNT * LOOPCOUNT
      },
      fn: bench_case.fn()
    })
    cb()
  })
}, function (err) {
  assert(err === null)
  suite.on('cycle', function (event) {
    console.log(String(event.target))
  }).on('complete', function () {
    console.log('Fastest is ' + this.filter('fastest').map('name'))
  }).run()
})

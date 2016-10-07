'use strict'
/* global describe it beforeEach afterEach before after */
const binary = require('node-pre-gyp')
const path = require('path')
const mmapObjPath = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MmapObject = require(mmapObjPath)
const expect = require('chai').expect
const temp = require('temp')
const childProcess = require('child_process')
const fs = require('fs')
const os = require('os')
const which = require('which')
const async = require('async')

const BigKeySize = 1000
const BiggerKeySize = 10000

describe('mmap-object', function () {
  before(function () {
    temp.track()
    this.dir = temp.mkdirSync('node-shared')
  })

  describe('Writer', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title + '_Writer')
      const ret = MmapObject(this.filename)
      this.shobj = ret.obj
      this.ctrl = ret.control
    })

    afterEach(function () {
      this.ctrl.close()
    })

    it('can be called as a constructor', function () {
      const dir = this.dir
      const ret = new MmapObject(path.join(dir, 'non-constructor'))
      expect(ret.obj).to.exist
      expect(ret.control.isClosed()).to.be.false
      ret.control.close()
      expect(ret.control.isClosed()).to.be.true
    })

    it('sets properties to a string', function () {
      this.shobj.my_string_property = 'my value'
      this.shobj['some other property'] = 'some other value'
      this.shobj['one more property'] = new Array(BigKeySize).join('A bunch of strings')
      expect(this.shobj.my_string_property).to.equal('my value')
      expect(this.shobj.my_string_property).to.equal('my value')
      expect(this.shobj['some other property']).to.equal('some other value')
      expect(this.shobj['one more property']).to.equal(new Array(BigKeySize).join('A bunch of strings'))
    })

    it('sets properties to a number', function () {
      this.shobj.my_number_property = 12
      expect(this.shobj.my_number_property).to.equal(12)
      this.shobj['some other number property'] = 0.2
      expect(this.shobj['some other number property']).to.equal(0.2)
    })

    it('sets properties to a buffer', function () {
      const buf = Buffer.from([0x62, 0x75, 0x66, 0x66, 0x65, 0x72])
      this.shobj.my_buffer_property = buf
      expect(this.shobj.my_buffer_property).to.deep.equal(buf)
      const buf2 = Buffer.from([0x62, 0x0, 0x66, 0x66, 0x65, 0x72])
      this.shobj.my_buffer_property2 = buf2
      expect(this.shobj.my_buffer_property2).to.deep.equal(buf2)
    })

    it('can delete properties', function () {
      this.shobj.should_be_deleted = 'please delete me'
      expect(this.shobj.should_be_deleted).to.equal('please delete me')
      expect(delete this.shobj.should_be_deleted).to.be.true
      expect(this.shobj.should_be_deleted).to.be.undefined
    })

    it('has enumerable and writable properties', function () {
      this.shobj.akey = 'avalue'
      expect(this.shobj.propertyIsEnumerable('akey')).to.be.true
      expect(Object.getOwnPropertyDescriptor(this.shobj, 'akey').writable).to.be.true
    })

    it('gets a property', function () {
      this.shobj.another_property = 'whateever value'
      expect(this.shobj.another_property).to.equal('whateever value')
    })

    it('has undefined unset properties', function () {
      expect(this.shobj.noother_property).to.be.undefined
    })

    it('throws when writing after close', function () {
      const m = MmapObject(path.join(this.dir, 'closetest-write'))
      m.obj['first'] = 'value'
      expect(m.control.isClosed()).to.be.false
      expect(m.control.isOpen()).to.be.true
      m.control.close()
      expect(m.control.isClosed()).to.be.true
      expect(m.control.isOpen()).to.be.false
      expect(function () {
        m.obj['second'] = 'something'
      }).to.throw(/Cannot write to closed object./)
    })

    it('throws when deleting after close', function () {
      const m = new MmapObject(path.join(this.dir, 'closetest-del'))
      m.obj['first'] = 'value'
      expect(m.control.isClosed()).to.be.false
      expect(m.control.isOpen()).to.be.true
      m.control.close()
      expect(m.control.isClosed()).to.be.true
      expect(m.control.isOpen()).to.be.false
      expect(function () {
        delete m.obj.first
      }).to.throw(/Cannot delete from closed object./)
    })

    it('throws when attempting to delete symbol', function () {
      const self = this
      expect(function () {
        delete self.shobj[Symbol('first')]
      }).to.throw(/Symbol properties are not supported for delete./)
    })

    it('throws exception on write to symbol property', function () {
      const self = this
      expect(function () {
        self.shobj[Symbol('first')] = 'what'
      }).to.throw(/Symbol properties are not supported./)
    })

    it('bombs when file cannot hold enough stuff', function () {
      const filename = path.join(this.dir, 'bomb_me')
      const m = MmapObject(filename, 'rw', 10, 20, 4)
      m.obj['key'] = new Array(BigKeySize).join('big')
      expect(function () {
        m.obj['otherkey'] = new Array(BigKeySize).join('big')
      }).to.throw(/File needs to be larger but can only be resized in write-only mode./)
      m.control.close()
    })

    it('allows numbers as property names', function () {
      this.shobj[1] = 'what'
      expect(this.shobj[1]).to.equal('what')
    })

    it('avoids getting too big when rewriting the same key over and over', function () {
      const filename = path.join(this.dir, 'bomb_me_2')
      const smallobj = MmapObject(filename, 'rw', 2, 2, 2)
      for (let i = 0; i < 200002; i++) {
        smallobj.obj['key'] = `a chunk of data: ${i}`
      }
      smallobj.control.close()
    })
  })

  describe('Write-onlyer', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title + '_Write_onlyer')
      const ret = MmapObject(this.filename, 'wo')
      this.shobj = ret.obj
      this.ctrl = ret.control
    })

    afterEach(function () {
      this.ctrl.close()
    })

    it('sets properties to a string', function () {
      this.shobj.my_string_property = 'my value'
      this.shobj['some other property'] = 'some other value'
      this.shobj['one more property'] = new Array(BigKeySize).join('A bunch of strings')
      expect(this.shobj.my_string_property).to.equal('my value')
      expect(this.shobj.my_string_property).to.equal('my value')
      expect(this.shobj['some other property']).to.equal('some other value')
      expect(this.shobj['one more property']).to.equal(new Array(BigKeySize).join('A bunch of strings'))
    })

    it('sets properties to a number', function () {
      this.shobj.my_number_property = 12
      expect(this.shobj.my_number_property).to.equal(12)
      this.shobj['some other number property'] = 0.2
      expect(this.shobj['some other number property']).to.equal(0.2)
    })

    it('can delete properties', function () {
      this.shobj.should_be_deleted = 'please delete me'
      expect(this.shobj.should_be_deleted).to.equal('please delete me')
      expect(delete this.shobj.should_be_deleted).to.be.true
      expect(this.shobj.should_be_deleted).to.be.undefined
    })

    it('has enumerable and writable properties', function () {
      this.shobj.akey = 'avalue'
      expect(this.shobj.propertyIsEnumerable('akey')).to.be.true
      expect(Object.getOwnPropertyDescriptor(this.shobj, 'akey').writable).to.be.true
    })

    it('gets a property', function () {
      this.shobj.another_property = 'whateever value'
      expect(this.shobj.another_property).to.equal('whateever value')
    })

    it('has undefined unset properties', function () {
      expect(this.shobj.noother_property).to.be.undefined
    })

    it('throws when attempting to delete symbol', function () {
      const self = this
      expect(function () {
        delete self.shobj[Symbol('first')]
      }).to.throw(/Symbol properties are not supported for delete./)
    })

    it('bombs on write to symbol property', function () {
      const self = this
      expect(function () {
        self.shobj[Symbol('first')] = 'what'
      }).to.throw(/Symbol properties are not supported./)
    })

    it('grows small files', function () {
      const filename = path.join(this.dir, 'grow_me')
      const m = MmapObject(filename, 'wo', 1, 200)
      expect(fs.statSync(filename)['size']).to.equal(1024)
      m.obj['key'] = new Array(BigKeySize).join('big')
      expect(fs.statSync(filename)['size']).to.above(1024)
    })

    it('bombs when file gets bigger than the max size', function () {
      const filename = path.join(this.dir, 'bomb_me_3')
      const m = MmapObject(filename, 'wo', 12, 16, 4)
      m.obj['key'] = new Array(BigKeySize).join('big')
      expect(function () {
        m.obj['otherkey'] = new Array(BigKeySize).join('big')
      }).to.throw(/File grew too large./)
      m.control.close()
    })
  })

  describe('Informational methods:', function () {
    before(function () {
      const m = MmapObject(path.join(this.dir, 'free_memory_file'))
      this.obj = m.obj
      this.ctrl = m.control
    })

    it('has get_free_memory', function () {
      const initial = this.ctrl.get_free_memory()
      this.obj.gfm = new Array(BigKeySize).join('Data')
      const final = this.ctrl.get_free_memory()
      expect(initial - final).to.be.above(12430)
    })

    it('has get_size', function () {
      const final = this.ctrl.get_size()
      expect(final).to.equal(5242880)
    })

    describe('file with some values', function () {
      // These are dependent && done in sequence.
      before(function () {
        this.m = MmapObject(path.join(this.dir, 'bucket_counter'), 'rw', 5, 1000, 4)
      })

      it('has bucket_count', function () {
        expect(this.m.control.bucket_count()).to.equal(4)
        this.m.obj.one = 'value'
        this.m.obj.two = 'value'
        this.m.obj.three = 'value'
        this.m.obj.four = 'value'
        expect(this.m.control.bucket_count()).to.equal(4)
        this.m.obj.five = 'value'
        expect(this.m.control.bucket_count()).to.equal(8)
      })

      it('has max_bucket_count', function () {
        const final = this.m.control.max_bucket_count()
        expect(final).to.equal(512)
      })

      it('has load_factor', function () {
        const final = this.m.control.load_factor()
        expect(final).to.equal(0.625)
      })

      it('has max_load_factor', function () {
        const final = this.m.control.max_load_factor()
        expect(final).to.equal(1.0)
      })

      it('has fileFormatVersion', function () {
        const version = this.m.control.fileFormatVersion();
        expect(version).to.equal(1);
      })
    })
  })

  describe('Opener', function () {
    before(function () {
      this.testfile = path.join(this.dir, 'openertest')
      const m = MmapObject(this.testfile, 'wo')
      const writer = m.obj
      writer['first'] = 'value for first'
      writer['second'] = 0.207879576
      this.bigKey = new Array(BigKeySize).join('fourty-nine thousand nine hundred fifty bytes long')
      writer[this.bigKey] = new Array(BiggerKeySize).join('six hundred seventy nine thousand nine hundred thirty two bytes long')
      writer['samekey'] = 'first value'
      writer['samekey'] = writer['samekey'] + ' and a new value too'
      writer[12345] = 'numberkey'
      writer['12346'] = 'numberkey2'
      writer.should_be_deleted = 'I should not exist!'
      delete writer.should_be_deleted
      m.control.close()
      this.n = MmapObject(this.testfile, 'ro')
      this.reader = this.n.obj
      this.control = this.n.control
    })

    it('works when Object.assigned', function () {
      const obj = Object.assign({}, this.reader)
      expect(obj.first).to.equal('value for first')
    })

    it('works when inherited', function () {
      const obj = Object.create(this.n)
      expect(obj.obj.first).to.equal('value for first')
    })

    it('works across copies', function () {
      const newfile = path.join(this.dir, 'copiertest')
      fs.writeFileSync(newfile, fs.readFileSync(this.testfile))
      const m = MmapObject(newfile)
      expect(m.obj.first).to.equal('value for first')
    })

    it('works across processes', function (done) {
      process.env.TESTFILE = this.testfile
      const child = childProcess.fork(which.sync('mocha'), ['./test/util-interprocess.js'])
      child.on('exit', function (exitCode) {
        expect(child.signalCode).to.be.null
        expect(exitCode, 'error from util-interprocess.js').to.equal(0)
        done()
      })
    })

    it('throws exception on non-existing file', function () {
      expect(function () {
        const obj = MmapObject('/tmp/no_file_at_all', 'ro')
        expect(obj).to.be.null
      }).to.throw(/.tmp.no_file_at_all does not exist.|.tmp.no_file_at_all: No such file or directory/)
    })

    it('throws exception on a zero-length file', function () {
      const newfile = path.join(this.dir, 'zerolength')
      fs.appendFileSync(newfile, '')
      expect(function () {
        new MmapObject(newfile)
      }).to.throw(/zerolength is an empty file./)
    })

    it('throws exception on a corrupt file', function () {
      const newfile = path.join(this.dir, 'corrupt')
      fs.appendFileSync(newfile, 'CORRUPTION')
      expect(function () {
        MmapObject(newfile).obj
      }).to.throw(/Can't open file .*\/corrupt: boost::interprocess_exception::library_error/)
    })
    it('read after close gives exception', function () {
      const m = MmapObject(this.testfile)
      expect(m.obj.first).to.equal('value for first')
      expect(m.control.isClosed()).to.be.false
      expect(m.control.isOpen()).to.be.true
      m.control.close()
      expect(m.control.isClosed()).to.be.true
      expect(m.control.isOpen()).to.be.false
      expect(function () {
        expect(m.obj.first).to.equal('value for first')
      }).to.throw(/Cannot read from closed object./)
    })

    it('cannot set properties', function () {
      const reader = this.reader
      expect(function () {
        reader.my_string_property = 'my value'
      }).to.throw(/Cannot write to read-only object./)
    })

    it('can get string properties', function () {
      expect(this.reader.first).to.equal('value for first')
    })

    it('can set/get numeric properties', function () {
      expect(this.reader['12345']).to.equal('numberkey')
      expect(this.reader[12346]).to.equal('numberkey2')
    })

    it('throws when deleting numeric properties', function () {
      const self = this
      expect(function () {
        delete self.reader[12345]
      }).to.throw(/Cannot delete from read-only object./)
    })

    it('can get keys', function () {
      expect(this.reader).to.have.keys(['first', 'second', this.bigKey, '12345', '12346', 'samekey'])
    })

    it('has enumerable but read-only properties', function () {
      expect(this.reader.propertyIsEnumerable('first')).to.be.true
      expect(Object.getOwnPropertyDescriptor(this.reader, 'first').writable).to.be.false
    })

    it('bombs on bad file', function () {
      expect(function () {
        const obj = MmapObject('/dev/null')
        expect(obj).to.not.exist
      }).to.throw(/.dev.null is not a regular file./)
    })

    it('throws exception on bad file', function () {
      if (os.platform() !== 'linux') return this.skip()
      expect(function () {
        const obj = MmapObject(path.join(__dirname, '..', 'testdata', 'badfile.bin'))
        expect(obj).to.not.exist
      }).to.throw(/File .*badfile.bin appears to be corrupt/)
    })

    it('throws exception on another bad file', function () {
      if (os.platform() === 'darwin' && /^v[45]\./.test(process.version)) {
        return this.skip() // Issues with these platforms on Travis
      }
      expect(function () {
        const obj = MmapObject(path.join(__dirname, '..', 'testdata', 'badfile2.bin'))
        expect(obj).to.not.exist
      }).to.throw(/File .*badfile2.bin appears to be corrupt/)
    })

    it('throws when attempting to delete property', function () {
      const self = this
      expect(function () {
        delete self.reader.first
      }).to.throw(/Cannot delete from read-only object./)
    })

    it('can close asynchronously', function (done) {
      MmapObject(this.testfile).control.close(done)
    })

    it('can open and close rapidly in a subprocess', function (done) {
      this.timeout(30000) // Can take a little longer
      process.env.TESTFILE = this.testfile
      async.times(10, function (n, next) {
        const child = childProcess.fork('./test/util-closer.js')
        child.on('exit', function (exitCode) {
          expect(child.signalCode).to.be.null
          expect(exitCode, 'error from util-closer.js').to.equal(0)
          next()
        })
      }, done)
    })

    it('throws when closing a closed object', function () {
      const m = MmapObject(this.testfile)
      m.control.close()
      expect(function () {
        m.control.close()
      }).to.throw(/Attempted to close a closed object./)
    })

    it('feeds error to callback when closing a closed object asynchronously', function (cb) {
      const obj = MmapObject(this.testfile)
      obj.control.close()
      obj.control.close(function (err) {
        expect(err).to.be.an('error')
        expect(err).to.match(/Attempted to close a closed object./)
        cb()
      })
    })

    it('implements the iterator protocol', function () {
      let count = 0
      let done
      const iterator = this.reader[Symbol.iterator]()
      do {
        const obj = iterator.next()
        const key = obj[0]
        const value = obj[1]
        expect(this.reader[key]).to.deep.equal(value)
        count++
        done = obj.done
      } while(!done)
      expect(count).to.equal(7)
    })
  })

  describe('Object comparison', function () {
    before(function () {
      const testfile1 = path.join(this.dir, 'prototest1')
      this.w = MmapObject(testfile1)
      this.writer1 = this.w.obj
      this.writer1['first'] = 'value for first'
      const testfile2 = path.join(this.dir, 'prototest2')
      this.x = MmapObject(testfile2)
      this.writer2 = this.x.obj
      this.writer2['first'] = 'value for first'
      this.reader1 = MmapObject(testfile1).obj
      this.reader2 = MmapObject(testfile2).obj
    })

    after(function () {
      this.w.control.close()
      this.x.control.close()
    })

    it('all creators are equal', function () {
      const writer1Prototype = Object.getPrototypeOf(this.writer1)
      const writer2Prototype = Object.getPrototypeOf(this.writer2)
      expect(writer1Prototype).to.equal(writer2Prototype)
      expect(writer1Prototype).to.equal(writer2Prototype)
      expect(writer2Prototype).to.equal(writer2Prototype)
    })

    it('openers are equal', function () {
      const reader1Prototype = Object.getPrototypeOf(this.reader1)
      const reader2Prototype = Object.getPrototypeOf(this.reader2)
      expect(reader1Prototype).to.equal(reader1Prototype)
      expect(reader1Prototype).to.equal(reader2Prototype)
      expect(reader2Prototype).to.equal(reader2Prototype)
    })
    // Add a "openers and creators why not" thing here.
  })
  describe('Still can read old format', function () {
    before(function () {
      const oldFormatFile = path.join(__dirname, '..', 'testdata', `previous-format-${os.platform()}.bin`)
      this.oldformat = MmapObject(oldFormatFile)
    })

    after(function () {
      this.oldformat.control.close()
    })

    it('reads string properties', function () {
      expect(this.oldformat.obj.my_string_property).to.equal('Some old value')
      expect(this.oldformat.obj['some other property']).to.equal('some other old value')
      expect(this.oldformat.obj['one more property']).to.deep.equal(new Array(BigKeySize).join('A giant bunch of strings'))
    })

    it('reads number properties', function () {
      expect(this.oldformat.obj.my_number_property).to.equal(27)
      expect(this.oldformat.obj['some other number property']).to.equal(23.42)
    })
  })
  describe('read-write objects', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title + '_Read_writer')
      this.shobj = MmapObject(this.filename)
    })

    afterEach(function () {
      this.shobj.control.close()
    })
    it('works across multiple processes', function (done) {
      const CHILDCOUNT = 10
      let children = []
      let state = [0, 0, 0, 0, 0]
      for (let i = 0; i < CHILDCOUNT; i++) {
        children[i] = childProcess.fork('./test/util-rw-process.js', [this.filename])
        children[i].on('exit', function (exit_code) {
          expect(children[i].signalCode).to.be.null
          expect(exit_code, 'error from util-rw-process.js').to.equal(0)
          // Everyone should have gone through the entire process
          // before anyone attempts to exit.
          expect(state[0]).to.equal(CHILDCOUNT)
          expect(state[1]).to.equal(CHILDCOUNT)
          expect(state[2]).to.equal(CHILDCOUNT)
          expect(state[3]).to.equal(CHILDCOUNT)
          state[4]++
          if (state[4] === CHILDCOUNT) { // All accounted for, we're done.
            done()
          }
        })
      }
      let shobj = this.shobj
      shobj.obj['one'] = 'first'
      const handler = function (child) {
        return function (msg) {
          switch (msg) {
            case 'started':
              expect(state[0]).to.be.below(CHILDCOUNT)
              child.send('read')
              state[0]++
              break
            case 'first': // Make sure it's reading anything.
              expect(state[1]).to.be.below(CHILDCOUNT)
              state[1]++
              if (state[1] === CHILDCOUNT) { // All children are synced
                shobj.control.writeLock(unLock => { // Lock the shared object.
                  shobj.obj['one'] = 'second' // Set to "wrong" value.
                  children.forEach(child => {
                    child.send('read') // All children's reads should be blocked by the writelock.
                  })
                  setTimeout(() => { // Give children time to (potentially) fail
                    shobj.obj['one'] = 'third' // Set to "right" value
                    unLock() // Unblock children's read
                  }, 200)
                })
              }
              break
            case 'second': // Children should NEVER read this
              return done(new Error('Locking is not working.'))
            case 'third': // Child read the right thing!
              expect(state[2]).to.be.below(CHILDCOUNT)
              state[2]++
              if (state[2] === CHILDCOUNT) { // Send them on a writing spree.
                children.forEach(child => {
                  child.send('write')
                })
              }
              break
            case 'fourth': // It wrote then read and was happy.
              expect(state[3]).to.be.below(CHILDCOUNT)
              state[3]++
              if (state[3] === CHILDCOUNT) { // All synced, shut them down.
                children.forEach(child => {
                  child.send('exit')
                })
              }
              break
            default: // Something wrong happened here.
              expect.fail(`Didn't expect ${msg}`)
              break
          }
        }
      }
      children.forEach(function (child) {
        child.on('message', handler(child))
      })
    })
  })
  describe('write-only objects', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title + '_Write_only_obj')
    })

    it('cannot open wo if already open rw', function () {
      const fn = this.filename
      expect(function () {
        MmapObject(fn, 'rw')
        MmapObject(fn, 'wo')
      }).to.throw(/Cannot lock for write-only, another process has this file open./)
    })
    it('cannot open wo if already open ro', function () {
      const fn = this.filename
      expect(function () {
        const obj = MmapObject(fn, 'rw')
        obj['a'] = 'b'
        obj.control.close()
        MmapObject(fn, 'ro')
        MmapObject(fn, 'wo')
      }).to.throw(/Cannot lock for write-only, another process has this file open./)
    })
    it('cannot open rw if already open wo', function () {
      const fn = this.filename
      expect(function () {
        MmapObject(fn, 'wo')
        MmapObject(fn, 'rw')
      }).to.throw(/Cannot open, another process has this open write-only./)
    })
    it('cannot open ro if already open wo', function () {
      const fn = this.filename
      expect(function () {
        MmapObject(fn, 'wo')
        MmapObject(fn, 'ro')
      }).to.throw(/Cannot open, another process has this open write-only./)
    })
  })
})

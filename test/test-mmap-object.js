'use strict'
/* global describe it beforeEach afterEach before after */
const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MMO = require(mmap_obj_path)
const expect = require('chai').expect
const temp = require('temp')
const child_process = require('child_process')
const fs = require('fs')
const os = require('os')
const which = require('which')

const BigKeySize = 1000
const BiggerKeySize = 10000

describe('mmap-object', function () {
  before(function () {
    temp.track()
    this.dir = temp.mkdirSync('node-shared')
  })

  describe('Writer', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title)
      const ret = MMO(this.filename)
      this.shobj = ret.obj
      this.ctrl = ret.control
    })

    afterEach(function () {
      this.ctrl.close()
    })

    it('can be called as a constructor', function () {
      const dir = this.dir
      const ret = new MMO(path.join(dir, 'non-constructor'))
      expect(ret.obj).to.exist
      expect(ret.control.isClosed()).to.equal(false)
      ret.control.close()
      expect(ret.control.isClosed()).to.equal(true)
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

    it('throws when writing after close', function () {
      const m = MMO(path.join(this.dir, 'closetest'))
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
      const m = new MMO(path.join(this.dir, 'closetest'))
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

    it('bombs on write to symbol property', function () {
      const self = this
      expect(function () {
        self.shobj[Symbol('first')] = 'what'
      }).to.throw(/Symbol properties are not supported./)
    })

    it('bombs when file cannot hold enough stuff', function () {
      const filename = path.join(this.dir, 'bomb_me')
      const m = MMO(filename, 'rw', 10000, 20000, 4)
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
  })

  describe('Write-onlyer', function () {
    beforeEach(function () {
      this.filename = path.join(this.dir, this.currentTest.title)
      const ret = MMO(this.filename, 'wo')
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
      const m = MMO(filename, 'wo', 1, 100)
      expect(fs.statSync(filename)['size']).to.equal(1024)
      m.obj['key'] = 'value'
      m.obj['key2'] = new Array(BigKeySize).join('big')
      expect(fs.statSync(filename)['size']).to.above(1024)
    })

    it('bombs when file gets bigger than the max size', function () {
      const filename = path.join(this.dir, 'bomb_me')
      const m = MMO(filename + "_wo", 'wo', 10, 20, 4)
      m.obj['key'] = new Array(BigKeySize).join('big')
      expect(function () {
        m.obj['otherkey'] = new Array(BigKeySize).join('big')
      }).to.throw(/File grew too large./)
      m.control.close()
    })
  })

  describe('Informational methods:', function () {
    before(function () {
      const m = MMO(path.join(this.dir, 'free_memory_file'))
      this.obj = m.obj
      this.ctrl = m.control
    })

    it('get_free_memory', function () {
      const initial = this.ctrl.get_free_memory()
      this.obj.gfm = new Array(BigKeySize).join('Data')
      const final = this.ctrl.get_free_memory()
      expect(initial - final).to.be.above(12432)
    })

    it('get_size', function () {
      const final = this.ctrl.get_size()
      expect(final).to.equal(5242880)
    })

    describe('file with some values', function () {
      // These are dependent && done in sequence.
      before(function () {
        this.m = MMO(path.join(this.dir, 'bucket_counter'), 'rw', 5, 1000, 4)
      })

      it('bucket_count', function () {
        expect(this.m.control.bucket_count()).to.equal(4)
        this.m.obj.one = 'value'
        this.m.obj.two = 'value'
        this.m.obj.three = 'value'
        this.m.obj.four = 'value'
        expect(this.m.control.bucket_count()).to.equal(4)
        this.m.obj.five = 'value'
        expect(this.m.control.bucket_count()).to.equal(8)
      })

      it('max_bucket_count', function () {
        const final = this.m.control.max_bucket_count()
        expect(final).to.equal(512)
      })

      it('load_factor', function () {
        const final = this.m.control.load_factor()
        expect(final).to.equal(0.625)
      })

      it('max_load_factor', function () {
        const final = this.m.control.max_load_factor()
        expect(final).to.equal(1.0)
      })
    })
  })

  describe('Opener', function () {
    before(function () {
      this.testfile = path.join(this.dir, 'openertest')
      const m = MMO(this.testfile, 'wo')
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
      const n = MMO(this.testfile, 'ro')
      this.reader = n.obj
    })

    it('works when Object.assigned', function () {
      const obj = Object.assign({}, this.reader)
      expect(obj.first).to.equal('value for first')
    })

    it('works across copies', function () {
      const newfile = path.join(this.dir, 'copiertest')
      fs.writeFileSync(newfile, fs.readFileSync(this.testfile))
      const m = MMO(newfile)
      expect(m.obj.first).to.equal('value for first')
    })

    it('works across processes', function (done) {
      process.env.TESTFILE = this.testfile
      const child = child_process.fork(which.sync('mocha'), ['./test/util-interprocess.js'])
      child.on('exit', function (exit_code) {
        expect(child.signalCode).to.be.null
        expect(exit_code, 'error from util-interprocess.js').to.equal(0)
        done()
      })
    })

    it('bombs on non-existing file', function () {
      expect(function () {
        MMO('/tmp/no_file_at_all', 'ro')
      }).to.throw(/.tmp.no_file_at_all does not exist.|.tmp.no_file_at_all: No such file or directory/)
    })
    it('read after close gives exception', function () {
      const m = MMO(this.testfile)
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
        const obj = MMO('/dev/null')
        expect(obj).to.not.exist
      }).to.throw(/.dev.null is not a regular file./)
    })

    it('throws when attempting to delete property', function () {
      const self = this
      expect(function () {
        delete self.reader.first
      }).to.throw(/Cannot delete from read-only object./)
    })

    it('throws when closing a closed object', function () {
      const m = MMO(this.testfile)
      m.control.close()
      expect(function () {
        m.control.close()
      }).to.throw(/Attempted to close a closed object./)
    })
  })

  describe('Object comparison', function () {
    before(function () {
      const testfile1 = path.join(this.dir, 'prototest1')
      this.w = MMO(testfile1)
      this.writer1 = this.w.obj
      this.writer1['first'] = 'value for first'
      const testfile2 = path.join(this.dir, 'prototest2')
      this.x = MMO(testfile2)
      this.writer2 = this.x.obj
      this.writer2['first'] = 'value for first'
      this.reader1 = MMO(testfile1).obj
      this.reader2 = MMO(testfile2).obj
    })

    after(function () {
      this.w.control.close()
      this.x.control.close()
    })

    it('all creators are equal', function () {
      const writer1_prototype = Object.getPrototypeOf(this.writer1)
      const writer2_prototype = Object.getPrototypeOf(this.writer2)
      expect(writer1_prototype).to.equal(writer2_prototype)
      expect(writer1_prototype).to.equal(writer2_prototype)
      expect(writer2_prototype).to.equal(writer2_prototype)
    })

    it('openers are equal', function () {
      const reader1_prototype = Object.getPrototypeOf(this.reader1)
      const reader2_prototype = Object.getPrototypeOf(this.reader2)
      expect(reader1_prototype).to.equal(reader1_prototype)
      expect(reader1_prototype).to.equal(reader2_prototype)
      expect(reader2_prototype).to.equal(reader2_prototype)
    })
    // Add a "openers and creators why not" thing here.
  })

  describe('Still can read old format', function () {
    before(function () {
      // The test file is only readable on Linux.
      if (os.platform() !== 'linux') {
        this.skip()
      } else {
        const old_format_file = `${__dirname}/previous-format.bin`
        this.oldformat = MMO(old_format_file)
      }
    })

    after(function () {
      if (os.platform() === 'linux') {
        this.oldformat.control.close()
      }
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
      this.filename = path.join(this.dir, this.currentTest.title)
      this.shobj = MMO(this.filename)
    })

    afterEach(function () {
      this.shobj.control.close()
    })
    it('works across multiple processes', function (done) {
      let children = []
      const CHILDCOUNT = 10
      for (let i = 0; i < CHILDCOUNT; i++) {
        children[i] = child_process.fork('./test/util-rw-process.js', [this.filename])
        children[i].on('exit', function (exit_code) {
          expect(children[i].signalCode).to.be.null
          expect(exit_code, 'error from util-rw-process.js').to.equal(0)
          done()
        })
      }
      let shobj = this.shobj
      shobj.obj['one'] = 'first'
      let state = [0, 0, 0, 0]
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
              if (state[1] === CHILDCOUNT) {
                shobj.control.writeLock(function (cb) {
                  shobj.obj['one'] = 'second'
                  children.forEach(function (child) {
                    child.send('read')
                  })
                  setTimeout(function () {
                    shobj.obj['one'] = 'third'
                    cb()
                  }, 200)
                })
              }
              break
            case 'third': // It read the right thing!
              expect(state[2]).to.be.below(CHILDCOUNT)
              state[2]++
              if (state[2] === CHILDCOUNT) { // Send them on a writing spree.
                children.forEach(function (child) {
                  child.send('write')
                })
              }
              break
            case 'fourth': // It wrote then read and was happy.
              expect(state[3]).to.be.below(CHILDCOUNT)
              state[3]++
              if (state[3] === CHILDCOUNT) { // Send them on a writing spree.
                done()
              }
              break
            default: // It read the wrong thing.  Probably 'second'.
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
      this.filename = path.join(this.dir, this.currentTest.title)
    })

    it('cannot open wo if already open rw', function () {
      const fn = this.filename
      expect(function () {
        MMO(fn, 'rw')
        MMO(fn, 'wo')
      }).to.throw(/Cannot lock for write-only, another process has this file open./)
    })
    it('cannot open wo if already open ro', function () {
      const fn = this.filename
      expect(function () {
        const obj = MMO(fn, 'rw')
        obj['a'] = 'b'
        obj.control.close()
        MMO(fn, 'ro')
        MMO(fn, 'wo')
      }).to.throw(/Cannot lock for write-only, another process has this file open./)
    })
    it('cannot open rw if already open wo', function () {
      const fn = this.filename
      expect(function () {
        MMO(fn, 'wo')
        MMO(fn, 'rw')
      }).to.throw(/Cannot open, another process has this open write-only./)
    })
    it('cannot open ro if already open wo', function () {
      const fn = this.filename
      expect(function () {
        MMO(fn, 'wo')
        MMO(fn, 'ro')
      }).to.throw(/Cannot open, another process has this open write-only./)
    })
  })
})

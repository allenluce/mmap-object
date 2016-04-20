'use strict'
/* global describe it beforeEach afterEach before after */
const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MmapObject = require(mmap_obj_path)
const expect = require('chai').expect
const temp = require('temp')
const child_process = require('child_process')
const fs = require('fs')
const os = require('os')
const which = require('which')

const BigKeySize = 1000
const BiggerKeySize = 10000

const methods = ['isClosed', 'isOpen', 'close', 'valueOf', 'toString',
                 'close', 'get_free_memory', 'get_size', 'bucket_count',
                 'max_bucket_count', 'load_factor', 'max_load_factor']

describe('mmap-object', function () {
  before(function () {
    temp.track()
    this.dir = temp.mkdirSync('node-shared')
  })

  describe('Writer', function () {
    beforeEach(function () {
      this.shobj = new MmapObject.Create(path.join(this.dir, this.currentTest.title))
    })

    afterEach(function () {
      this.shobj.close()
    })

    it('must be called as a constructor', function () {
      const dir = this.dir
      expect(function () {
        const obj = MmapObject.Create(path.join(dir, 'non-constructor'))
        expect(obj).to.not.exist
      }).to.throw(/Create must be called as a constructor./)
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

    it('has un-enumerable and read-only methods', function () {
      for (let method of methods) {
        expect(this.shobj.propertyIsEnumerable(method), `${method} is enumerable`).to.be.false
        expect(delete this.shobj[method], `${method} is writable`).to.be.false
      }
    })

    it('gets a property', function () {
      this.shobj.another_property = 'whateever value'
      expect(this.shobj.another_property).to.equal('whateever value')
    })

    it('has undefined unset properties', function () {
      expect(this.shobj.noother_property).to.be.undefined
    })

    it('throws when writing after close', function () {
      const obj = new MmapObject.Create(path.join(this.dir, 'closetest'))
      obj['first'] = 'value'
      expect(obj.isClosed()).to.be.false
      expect(obj.isOpen()).to.be.true
      obj.close()
      expect(obj.isClosed()).to.be.true
      expect(obj.isOpen()).to.be.false
      expect(function () {
        obj['second'] = 'something'
      }).to.throw(/Cannot write to closed object./)
    })

    it('throws when deleting after close', function () {
      const obj = new MmapObject.Create(path.join(this.dir, 'closetest'))
      obj['first'] = 'value'
      expect(obj.isClosed()).to.be.false
      expect(obj.isOpen()).to.be.true
      obj.close()
      expect(obj.isClosed()).to.be.true
      expect(obj.isOpen()).to.be.false
      expect(function () {
        delete obj.first
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

    it('grows small files', function () {
      const filename = path.join(this.dir, 'grow_me')
      const smallobj = new MmapObject.Create(filename, 500)
      expect(fs.statSync(filename)['size']).to.equal(500)
      smallobj['key'] = new Array(BigKeySize).join('big')
      expect(fs.statSync(filename)['size']).to.above(500)
    })

    it('bombs when file gets too big', function () {
      const filename = path.join(this.dir, 'bomb_me')
      const smallobj = new MmapObject.Create(filename, 500, 4, 20000)
      smallobj['key'] = new Array(BigKeySize).join('big')
      expect(function () {
        smallobj['otherkey'] = new Array(BigKeySize).join('big')
      }).to.throw(/File grew too large./)
    })

    it('allows numbers as property names', function () {
      this.shobj[1] = 'what'
      expect(this.shobj[1]).to.equal('what')
    })
  })

  describe('Informational methods:', function () {
    before(function () {
      this.obj = new MmapObject.Create(path.join(this.dir, 'free_memory_file'))
    })

    it('get_free_memory', function () {
      const initial = this.obj.get_free_memory()
      this.obj.gfm = new Array(BigKeySize).join('Data')
      const final = this.obj.get_free_memory()
      expect(initial - final).to.be.above(12432)
    })

    it('get_size', function () {
      const final = this.obj.get_size()
      expect(final).to.equal(5242880)
    })

    it('bucket_count', function () {
      this.obj = new MmapObject.Create(path.join(this.dir, 'bucket_counter'), 5000, 4)
      expect(this.obj.bucket_count()).to.equal(4)
      this.obj.one = 'value'
      this.obj.two = 'value'
      this.obj.three = 'value'
      this.obj.four = 'value'
      expect(this.obj.bucket_count()).to.equal(4)
      this.obj.five = 'value'
      expect(this.obj.bucket_count()).to.equal(8)
    })

    it('max_bucket_count', function () {
      const final = this.obj.max_bucket_count()
      expect(final).to.equal(512)
    })

    it('load_factor', function () {
      const final = this.obj.load_factor()
      expect(final).to.equal(0.625)
    })

    it('max_load_factor', function () {
      const final = this.obj.max_load_factor()
      expect(final).to.equal(1.0)
    })
  })

  describe('Opener', function () {
    before(function () {
      this.testfile = path.join(this.dir, 'openertest')
      const writer = new MmapObject.Create(this.testfile)
      writer['first'] = 'value for first'
      writer['second'] = 0.207879576
      this.bigKey = new Array(BigKeySize).join('fourty-nine thousand nine hundred fifty bytes long')
      writer[this.bigKey] = new Array(BiggerKeySize).join('six hundred seventy nine thousand nine hundred thirty two bytes long')
      writer['samekey'] = 'first value'
      writer['samekey'] = writer['samekey'] + ' and a new value too'
      writer.should_be_deleted = 'I should not exist!'
      delete writer.should_be_deleted
      writer.close()
      this.reader = new MmapObject.Open(this.testfile)
    })
    it('must be called as a constructor', function () {
      const testfile = this.testfile
      expect(function () {
        const obj = MmapObject.Open(testfile)
        expect(obj).to.not.exist
      }).to.throw(/Open must be called as a constructor./)
    })

    it('works across copies', function () {
      const newfile = path.join(this.dir, 'copiertest')
      fs.writeFileSync(newfile, fs.readFileSync(this.testfile))
      const new_reader = new MmapObject.Open(newfile)
      expect(new_reader.first).to.equal('value for first')
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
        const obj = new MmapObject.Open('/tmp/no_file_at_all')
        expect(obj).to.not.exist
      }).to.throw(/.tmp.no_file_at_all does not exist./)
    })

    it('read after close gives exception', function () {
      const obj = new MmapObject.Open(this.testfile)
      expect(obj.first).to.equal('value for first')
      expect(obj.isClosed()).to.be.false
      expect(obj.isOpen()).to.be.true
      obj.close()
      expect(obj.isClosed()).to.be.true
      expect(obj.isOpen()).to.be.false
      expect(function () {
        expect(obj.first).to.equal('value for first')
      }).to.throw(/Cannot read from closed object./)
    })

    it('cannot set properties', function () {
      const reader = this.reader
      expect(function () {
        reader.my_string_property = 'my value'
      }).to.throw(/Read-only object./)
    })

    it('can get stored strings', function () {
      expect(this.reader.first).to.equal('value for first')
    })

    it('can get keys', function () {
      expect(this.reader).to.have.keys(['first', 'second', this.bigKey, 'samekey'])
    })

    it('has enumerable but read-only properties', function () {
      expect(this.reader.propertyIsEnumerable('first')).to.be.true
      expect(Object.getOwnPropertyDescriptor(this.reader, 'first').writable).to.be.false
    })

    it('has un-enumerable and read-only methods', function () {
      for (let method of methods) {
        expect(this.reader.propertyIsEnumerable(method), `${method} is enumerable`).to.be.false
        expect(delete this.reader[method], `${method} is writable`).to.be.false
      }
    })

    it('bombs on bad file', function () {
      expect(function () {
        const obj = new MmapObject.Open('/dev/null')
        expect(obj).to.not.exist
      }).to.throw(/.dev.null is not a regular file./)
    })

    it('throws when closing a closed object', function () {
      const obj = new MmapObject.Open(this.testfile)
      obj.close()
      expect(function () {
        obj.close()
      }).to.throw(/Attempted to close a closed object./)
    })
  })

  describe('Object comparison', function () {
    before(function () {
      const testfile1 = path.join(this.dir, 'prototest1')
      this.writer1 = new MmapObject.Create(testfile1)
      this.writer1['first'] = 'value for first'
      const testfile2 = path.join(this.dir, 'prototest2')
      this.writer2 = new MmapObject.Create(testfile2)
      this.writer2['first'] = 'value for first'
      this.reader1 = new MmapObject.Open(testfile1)
      this.reader2 = new MmapObject.Open(testfile2)
    })

    after(function () {
      this.writer1.close()
      this.writer2.close()
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

    it('openers are not creators', function () {
      const reader_prototype = Object.getPrototypeOf(this.reader1)
      const writer_prototype = Object.getPrototypeOf(this.writer1)
      expect(reader_prototype).to.not.equal(writer_prototype)
    })
  })

  describe('Still can read old format', function () {
    before(function () {
      // The test file is only readable on Linux.
      if (os.platform() === 'darwin') {
        this.skip()
      } else {
        const old_format_file = `${__dirname}/previous-format.bin`
        this.oldformat = new MmapObject.Open(old_format_file)
      }
    })

    after(function () {
      if (os.platform() !== 'darwin') {
        this.oldformat.close()
      }
    })

    it('reads string properties', function () {
      expect(this.oldformat.my_string_property).to.equal('Some old value')
      expect(this.oldformat['some other property']).to.equal('some other old value')
      expect(this.oldformat['one more property']).to.deep.equal(new Array(BigKeySize).join('A giant bunch of strings'))
    })

    it('reads number properties', function () {
      expect(this.oldformat.my_number_property).to.equal(27)
      expect(this.oldformat['some other number property']).to.equal(23.42)
    })
  })
})

'use strict'
/* global describe it before */
/*
  This utility file is intended to run in a different process space
  than the main tests.  This helps to ensure that things do get
  written to disk properly.  Doing these tests in the same process
  space where the tests are written is problematic as Boost shares
  allocators.
*/

const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MMO = require(mmap_obj_path)
const expect = require('chai').expect

const BigKeySize = 1000
const BiggerKeySize = 10000

describe('Interprocess', function () {
  before(function () {
    const m = new MMO(process.env.TESTFILE)
    this.reader = m.obj
  })
  it('reads a string', function () {
    expect(this.reader.first).to.equal('value for first')
  })

  it('reads a string that was written twice', function () {
    expect(this.reader.samekey).to.equal('first value and a new value too')
  })

  it('reads a number', function () {
    expect(this.reader.second).to.equal(0.207879576)
  })
  it('reads large data', function () {
    const bigKey = new Array(BigKeySize).join('fourty-nine thousand nine hundred fifty bytes long')
    const bigValue = new Array(BiggerKeySize).join('six hundred seventy nine thousand nine hundred thirty two bytes long')
    expect(this.reader[bigKey]).to.equal(bigValue)
  })
})
         

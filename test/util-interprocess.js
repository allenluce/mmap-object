'use strict'
/* global describe it before */
/*
  This utility file is intended to run in a different process space
  than the main tests.  This helps to ensure that things do get
  written to disk properly.  Doing these tests in the same process
  space where the tests are written is problematic as Boost shares
  allocators.
*/

const MmapObject = require('../build/Release/mmap-object.node')
const expect = require('chai').expect

describe('Interprocess', function () {
  before(function () {
    this.reader = new MmapObject.Open(process.env.TESTFILE)
  })
  it('reads a string', function () {
    expect(this.reader.first).to.equal('value for first')
  })

  it('reads a number', function () {
    expect(this.reader.second).to.equal(0.207879576)
  })
  it('reads large data', function () {
    const bigKey = new Array(1000).join('fourty-nine thousand nine hundred fifty bytes long')
    const bigValue = new Array(10000).join('six hundred seventy nine thousand nine hundred thirty two bytes long')
    expect(this.reader[bigKey]).to.equal(bigValue)
  })
})
         

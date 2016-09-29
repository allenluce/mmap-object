'use strict'
/*
  This is intended to run in a different process space than the main
  tests. Its job is to do enough stuff that a GC is very likely. This
  test is to guard against the GC causing the background close to lose
  access to resources it needs.
*/

const binary = require('node-pre-gyp')
const path = require('path')
const mmap_obj_path = binary.find(path.resolve(path.join(__dirname, '../package.json')))
const MmapObject = require(mmap_obj_path)
const expect = require('chai').expect
const async = require('async')

function closeit (done) {
  const reader = MmapObject(process.env.TESTFILE)
  expect(reader.obj.first).to.equal('value for first')
  reader.control.close(done)
}

async.times(4000, function (n, next) {
  process.nextTick(closeit, next)
})

'use strict'
const sqlite3 = require('sqlite3')

const dbPath = process.argv[2]
const childNo = process.argv[3]

var db = new sqlite3.Database(dbPath)

process.on('message', function (msg) {
  switch (msg) {
    case 'write':
      // Do a bunch of writes based on our childNo here.
      const stmt = db.prepare('insert into info (child, which, value) values(?,?,?)')
      for (let i = 0; i < 100; i++) {
        stmt.run(childNo, i, 'boogabooga')
      }
      stmt.finalize()
      process.send('wrote') // Report that we're done.
      break
    case 'read':
      // Do a bunch of writes based on our childNo here.
      db.all('select child, which, value from info where child=?', function (err, rows) {
        console.log(rows)
        process.send('didread') // report that we're done
      })
      break
    case 'exit':
      process.exit()
      break
  }
})
process.send('started')

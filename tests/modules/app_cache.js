const a = require("./counter.js")
const b = require("./counter.js")
console.log("module-cache", a === b, a.count, b.count)

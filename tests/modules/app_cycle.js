const a = require("./cycle_a")
const b = require("./cycle_b")
console.log("module-cycle", a.name, a.bSaw, b.sawA)

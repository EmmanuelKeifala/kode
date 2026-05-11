exports.old = "captured"
const oldExports = exports
module.exports = { name: "current" }
const b = require("./cycle_reassign_b")
module.exports.bSaw = b.sawA
module.exports.oldStill = oldExports.old
module.exports.oldWasNotMutated = oldExports.name === undefined

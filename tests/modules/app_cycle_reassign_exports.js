const a = require("./cycle_reassign_a")
console.log("module-cycle-reassign", a.name, a.bSaw, a.oldStill, a.oldWasNotMutated)

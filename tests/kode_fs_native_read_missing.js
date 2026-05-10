const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const missing = scope.async(() => fs.read("tests/does-not-exist-native.txt", { as: "text" }))

  try {
    await missing
  } catch (err) {
    console.log("read-missing", err.code, err.operation, err.path)
  }
})

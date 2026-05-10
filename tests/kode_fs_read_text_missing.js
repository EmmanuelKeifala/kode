const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const missing = scope.async(() => fs.readText("tests/does-not-exist.txt"))

  try {
    await missing
  } catch (err) {
    console.log(err.code, err.operation, err.path)
  }
})

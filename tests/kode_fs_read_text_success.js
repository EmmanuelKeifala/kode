const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const text = scope.async(() => fs.readText("README.md"))
  console.log(await text)
})

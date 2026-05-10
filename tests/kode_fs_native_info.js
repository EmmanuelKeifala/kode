const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const existing = await scope.async(() => fs.info("README.md"))
  const missing = await scope.async(() => fs.info("tests/no-native-info.txt"))
  console.log("info", existing.kind, existing.mimeType, missing === null)
})

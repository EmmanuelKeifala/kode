const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const file = await scope.async(() => fs.read("README.md", { as: "text" }))
  console.log("read", file.text.includes("Kode Runtime"), file.info.kind, file.info.mimeType)
})

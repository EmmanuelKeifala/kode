const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const file = await scope.async(() => fs.read("README.md", { as: "text" }))
  console.log("read", file.text.includes("Kode is an experimental JavaScript runtime"), file.info.kind, file.info.mimeType)
})

const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const result = await scope.async(() => fs.write("tmp/kode-native/out/file.txt", "hello", { create: "parents" }))
  const file = await scope.async(() => fs.read("tmp/kode-native/out/file.txt", { as: "text" }))
  console.log("write-parents", result.bytesWritten, result.info.kind, file.text)
})

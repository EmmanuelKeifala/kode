const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const write = scope.async(() => fs.write("tmp/kode-native/missing-parent/file.txt", "hello"))

  try {
    await write
  } catch (err) {
    console.log("write-no-parents", err.code, err.operation, err.path)
  }
})

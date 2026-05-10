const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const timeout = Kode.timeout(1000)
  timeout.cancel()

  const task = scope.async(() => fs.write("tmp/kode-native/cancelled.txt", "nope", { create: "parents", signal: timeout.signal }))
  try {
    await task
  } catch (err) {
    console.log("write-cancelled", err.code, err.operation)
  }
})

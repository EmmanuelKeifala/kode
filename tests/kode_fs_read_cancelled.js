const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const timeout = Kode.timeout(1000)
  timeout.cancel()

  const task = scope.async(() => fs.read("README.md", { as: "text", signal: timeout.signal }))
  try {
    await task
  } catch (err) {
    console.log("read-cancelled", err.code, err.operation)
  }
})

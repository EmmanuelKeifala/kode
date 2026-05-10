Kode.scope(async (scope) => {
  const failing = scope.async(() => {
    throw new Error("boom")
  })

  try {
    await failing
  } catch (err) {
    console.log("caught", err.message)
  }

  const skipped = scope.async(() => {
    console.log("should-not-run")
    return "bad"
  })

  try {
    await skipped
  } catch (err) {
    console.log("cancelled", err.code, err.operation)
  }
})

Kode.scope(async (scope) => {
  const first = scope.async(async () => {
    await Kode.sleep(0)
    return "alpha"
  })
  const second = scope.async(async () => "beta")
  console.log("sleep-scope", await first, await second)
})

Kode.scope(async (scope) => {
  const first = scope.async(async () => "alpha")
  const second = scope.async(async () => "beta")
  console.log("top-level-scope", await first, await second)
})

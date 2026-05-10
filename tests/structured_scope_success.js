Kode.scope(async (scope) => {
  const a = scope.async(() => "alpha")
  const b = scope.async(() => "beta")

  console.log(await a, await b)
})

const before = Kode.activeOperations()

Kode.scope(async (scope) => {
  const during = Kode.activeOperations()
  console.log("during", during.scopes >= 1)

  const value = scope.async(() => "done")
  console.log(await value)
}).then(() => {
  const after = Kode.activeOperations()
  console.log("after", before.scopes, after.scopes, after.tasks)
})

const timeout = Kode.timeout(1000)
Kode.scope(async () => {
  timeout(() => {})
})

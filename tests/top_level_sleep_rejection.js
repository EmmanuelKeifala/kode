Kode.scope(async () => {
  await Kode.sleep(0)
  throw new Error("late boom")
})

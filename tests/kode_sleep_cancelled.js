Kode.scope(async () => {
  try {
    await Kode.sleep(10, { signal: Kode.timeout(0).signal })
  } catch (err) {
    console.log("sleep-cancelled", err.code, err.operation)
  }
})

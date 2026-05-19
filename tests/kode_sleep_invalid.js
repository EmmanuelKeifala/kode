try {
  Kode.sleep(-1)
} catch (err) {
  console.log("sleep-invalid", err.code, err.operation)
}

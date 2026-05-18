try {
  new TextDecoder("latin1")
} catch (err) {
  console.log("text-decoder-label", err.code, err.operation)
}

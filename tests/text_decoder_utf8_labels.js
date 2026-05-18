console.log(
  "text-decoder-labels",
  new TextDecoder("UTF-8").decode(new Uint8Array([104, 105])),
  new TextDecoder("UTF8").decode(new Uint8Array([104, 105]))
)

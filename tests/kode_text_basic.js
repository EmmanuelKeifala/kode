const original = Kode.text
try {
  Kode.text = { broken: true }
} catch (err) {}

const bytes = Kode.text.encode("hello")
console.log("kode-text", Kode.text === original, bytes instanceof Uint8Array, Kode.text.decode(bytes))

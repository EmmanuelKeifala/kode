console.log("args-script", Kode.args.script.endsWith("tests/kode_args_basic.js"))
console.log("args-values", Kode.args.values.join(","))

Object.freeze = value => value
try {
  Kode.args.extra = "mutable"
} catch (error) {
}
try {
  Kode.args.values.push("mutable")
} catch (error) {
}
console.log("args-frozen", Kode.args.extra === undefined, Kode.args.values.length === 2)

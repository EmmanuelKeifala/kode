const env = Kode.env.toObject()

console.log("env-has", Kode.env.has("KODE_ENV_TEST"))
console.log("env-get", Kode.env.get("KODE_ENV_TEST"))
console.log("env-missing", Kode.env.get("KODE_ENV_DOES_NOT_EXIST") === undefined)
console.log("env-object", env.KODE_ENV_TEST)

Object.freeze = value => value
const frozenEnv = Kode.env.toObject()
try {
  Kode.env.extra = "mutable"
} catch (error) {
}
try {
  frozenEnv.extra = "mutable"
} catch (error) {
}
console.log("env-frozen", Kode.env.extra === undefined, frozenEnv.extra === undefined)
console.log("env-proto", Object.prototype.hasOwnProperty.call(frozenEnv, "__proto__"), frozenEnv.__proto__)

try {
  Kode = {}
} catch (error) {
}
try {
  delete globalThis.Kode
} catch (error) {
}
console.log("kode-global-protected", typeof Kode.env.get, Array.isArray(Kode.args.values))

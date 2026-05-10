try {
  require("lodash")
} catch (err) {
  console.log("module-unsupported", err.code, err.operation)
}

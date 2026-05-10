const timeout = Kode.timeout(1000)
console.log("timeout-before", timeout.signal.aborted, timeout.signal.reason === undefined)
timeout.cancel()
console.log("timeout-after", timeout.signal.aborted, timeout.signal.reason.code, timeout.signal.reason.operation)

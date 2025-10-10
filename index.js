const fs = require('fs');

fs.readFile('indx.js', (err, data) => {
    if (err) {
        console.log(err);
    } else {
        console.log(data.toString());
    }
});
console.log("Hello from Kode!");
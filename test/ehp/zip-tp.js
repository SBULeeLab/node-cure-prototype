var fs = require('fs');
var zlib = require('zlib');

var gzip = zlib.createGzip();
var inp = fs.createReadStream('/tmp/raw.dat');
var out = fs.createWriteStream('/tmp/raw.gz');
 
console.log('JS: Piping /tmp/raw.dat into /tmp/raw.gz');
inp.pipe(gzip).pipe(out);
//console.log('JS: Done');

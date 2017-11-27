const fs = require('fs');
let iterationNumber = 30000;
if (process.argv.length > 2){
  iterationNumber = parseInt(process.argv[2],10);
}
const contentsToWrite = Buffer.allocUnsafe(64*1024);

for (let i = 0; i < iterationNumber; i++){
	fs.writeFileSync('benchmarks/fs_files/64Kb.txt'+i, contentsToWrite);
}


// read from the files
for (let i = 0; i < iterationNumber; i++){
	const contents = fs.readFileSync('benchmarks/fs_files/64Kb.txt'+i);
}


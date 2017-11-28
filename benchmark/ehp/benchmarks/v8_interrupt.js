let loop_iterations = 9999999999/9; // (1 billion - 1)/3
if (process.argv.length > 2){
  loop_iterations = parseInt(process.argv[2],10);
}

try
{
	for (let i = 0; i < loop_iterations; i++){
  		// intentionally blank
	}
}
catch(e)
{
}

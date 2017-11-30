from lib import analyze
from functools import partial
def json_response(line):
	last_line = float(line.split("\"throughput\"")[1].split(",")[0].split(" ")[-1])
	return 1/last_line * 1000, str(last_line)+"requests/second"

def text_response(line):
	l = float(line.split("\"throughput\"")[2].split(",")[0].split(" ")[-1])
	return 1/l * 1000, str(l)+"requests/second"


tests = [(json_response, 'response json'), (text_response, 'response text')]
		
import json	
print(json.dumps(analyze(tests, 'restify'))) 

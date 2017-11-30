from lib import analyze
from functools import partial
def parse_time(line):
	last_line = float(line.split(" ")[1].split("s")[0].split("m")[1])
	return last_line

tests = [(parse_time, 'real time')]
		
import json	
print(json.dumps(analyze(tests, 'webtorrent'))) 

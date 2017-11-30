from lib import analyze
from functools import partial
def mw(line):
	last_line = line.split(" ")
	print(last_line)
	return 1/ last_line


tests = [(mw, '100 middleware')]
		
import json	
print(json.dumps(analyze(tests, 'KOA'))) 

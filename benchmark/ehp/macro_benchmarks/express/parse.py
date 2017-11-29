from lib import analyze
from functools import partial
def mw(line):
	last_line = float(line.split(" ")[23])
	print(last_line)
	return 1/ last_line


tests = [(mw, '100 middleware')]
		
	
print(analyze(tests, 'EXPRESS')) 

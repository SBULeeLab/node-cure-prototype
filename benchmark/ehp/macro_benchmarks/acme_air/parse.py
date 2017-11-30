from lib import analyze
def get_average(line):
	last_line = line.split("summary =")[-1].split("Avg:")[0].split("=")[1].split("/s")[0]
	return (1/float(last_line)) * 1000


tests = [(get_average, 'ACME_AIR_REQS_SECOND_AFTER_10_MINUTES')]
		
import json	
print(json.dumps(analyze(tests, 'ACME_AIR')))

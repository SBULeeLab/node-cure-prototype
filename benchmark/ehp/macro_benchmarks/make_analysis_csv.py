def print_h():
	print('TEST_NAME,NODE_TYPE,MEAN,VARIANCE,OVERHEAD,UNIT,RESULTS')
def print_analysis(analysis):
	print(analysis['name'])
	print_h()
	for test in analysis['tests']:
		print_test(test)

def print_test(test):
	for result in test['results']:
		print_result(test, result)

def print_result(test, result):
	print(','.join([test['name'], str(result['file']), str(result['mean']), str(result['var']), str(result['overhead']), str(result['unit'])] + [str(x) for x in result['readable_results']]))



import json,subprocess
analyses = json.loads(subprocess.check_output(['python', 'make_analysis_json.py']))
for analysis in analyses:
	print("")
	print("")
	print("")
	print_analysis(analysis)	

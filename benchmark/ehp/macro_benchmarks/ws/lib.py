files = ["watchdog_precise.result", "watchdog_lazy.result", "original.result"]
def mean(numbers):
    return float(sum(numbers)) / max(len(numbers), 1)
def variance(numbers):
    average = mean(numbers)
    v = 0
    for i in numbers:
        v += (average - i) ** 2
    return v / float(max(len(numbers),1))

def get_reqs(f,f2):
	rs = []
	try:
		with open(f) as fi:
			for line in fi.readlines():
				try:
					r = f2(line)
					rs.append(r)
				except:
					pass
	except:
		pass
	return {'file': f, 'results':rs, 'mean': mean(rs), 'var': variance(rs)}

def get_result(test):
	results = []
	for f in files:
		results.append(get_reqs(f, test))
	orig_mean = float(results[-1]['mean'])
	for r in results:
		r['overhead'] = r['mean']/max(orig_mean, 0.0000000000000000001)
	return results

def analyze(tests, name):
	tests_l = []
	for test_f, test_name in tests:
		test = {'name':test_name, 'results':get_result(test_f)}
		tests_l.append(test)
	return {'name': name, 'tests': tests_l}


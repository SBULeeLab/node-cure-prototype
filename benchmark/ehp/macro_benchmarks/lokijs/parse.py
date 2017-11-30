from lib import analyze
from functools import partial
def mw(line, idx):
	last_line = line.split("ops/s")
	l = float(last_line[idx].split('(')[-1].split(')')[0])
	#print(last_line)
	return (1/ l ) * 1000

def g(idx,name):
	return (partial(mw, idx=idx), name)
#tests = [
#	g(0,'100 middleware'),
#	g(1,'coll.geti()')	
#	]
s= 'load(insert),coll.get(), coll.by(),NON-INDEXED coll.find,NON-INDEXED chained find,NON INDEXED loki dynamic view first find,NONINDEXED loki dynamic view subsequent finds,BINARY INDEX coll.find(), BINARY INDEXED chained find, BINARY INDEX loki dynamic view fisrt find, BINARY INDEX loki dynamic view subsequent find, UNINDEXED load(individual), UNINDEXED load (batch),INDEXED LAZY load(individual), INDEXED LAZY load(batch), INDEXED ADAPTIVE load(individual), INDEXED ADAPTIVE load(batch), UNINDEXED coll.find, UNINDEXED interlaced inserts + coll.find, UNINDEXED removes and coll.find, UNINDEXED interlaced updates and coll.find,LAZY coll.find, LAZYinsert + find, LAZY remove + find, LAZY update + find, ADAPTIVE coll.find, ADAPTIVE insert + find, ADAPTIVE remove + find, ADAPTIVE update + find'.split(',')
tests = [g(idx,val) for idx,val in enumerate(s)]
import json	
print(json.dumps(analyze(tests, 'lokijs'))) 

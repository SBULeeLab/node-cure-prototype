#!/usr/bin/env python
# -*- coding: utf-8 -*-
from lib import analyze
from functools import partial
def f(line, idx):
	l = []
	ll = line.split("•")
	s = ll[idx]
	z = s.split('x')
	if len(z) >= 2:
		l.append(float(''.join(s.split('x')[-1].split("ops")[0].split(','))))
	return (1/l[0]) * 1000, str(l[0])+"operations/second"

def g(i,name):
	return (partial(f,idx=i), name)
tests = [g(2,"frame unmasked (64 B)")
]
t = [
 "route_with_no_verb", 
 "route_with_GET_verb", 
 "route_with_POST_verb", 
 "route_with_dynamic_param", 
 "route_with_wildcard", 
 "route_with_rege",
 "respond_with_string", 
 "respond_with_json" ,
 "reflect_one_param" ,
 "reflect_all_params" ,
 "route_with_no_verb" ,
 "route_with_GET_verb" ,
 "route_with_POST_verb" ,
 "route_with_dynamic_param", 
 "route_with_wildcard" ,
 "route_with_rege",
 "respond_with_string", 
 "respond_with_json" ,
 "reflect_one_param" ,
 "reflect_all_params" ,
 "route_with_no_verb" ,
 "route_with_GET_verb" ,
 "route_with_POST_verb" ,
 "route_with_dynamic_param", 
 "route_with_wildcard" ,
 "route_with_rege",
 "respond_with_string", 
 "respond_with_json" ,
 "reflect_one_param" ,
 "reflect_all_params" 
]
tests = [g(i+1,v) for i,v in enumerate(t)]
import json	
print(json.dumps(analyze(tests, 'sails'))) 

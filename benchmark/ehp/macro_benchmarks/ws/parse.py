from lib import analyze
from functools import partial
def f(line, idx):
	l = []
	ll = line.split("ops/s")
	s = ll[idx]
	z = s.split('x')
	if len(z) >= 2:
		l.append(float(''.join(s.split('x')[-1].split(','))))
	return (1/l[0]) * 1000

def g(i,name):
	return (partial(f,idx=i-1), name)
tests = [g(1,"frame, unmasked (64 B)"),
	g(2,"frame, masked (64 B)"),
	g(3,"frame, unmasked (16 KiB)"),
	g(4,"frame, masked (16 KiB)"),
	g(5,"frame, unmasked (64 KiB)"),	
	g(6,"frame, masked (16 KiB)"),
	g(7,"frame, unmasked (200 KiB)"),
	g(8,"frame, masked (200 KiB)"),

	g(9,"frame, unmasked (1 MiB)"),
	g(10,"frame, masked (1 MiB)"),
	g(11, "ping frame (5 bytes payload)"),
	g(12, "ping frame (no payload)"),
	g(13, "close frame (no payload)"),
	g(14,"text frame (20 bytes payload)"),
	
	g(15,"binary frame (125 bytes payload)"),
	g(16,"binary frame (65535 bytes payload)"),
	g(17,"binary frame (200 KiB payload)"),
	g(18,"binary frame (1 MiB payload)"),
]	
	
print(analyze(tests, 'ws')) 

import os

class cd:
    """Context manager for changing the current working directory"""
    def __init__(self, newPath):
        self.newPath = os.path.expanduser(newPath)

    def __enter__(self):
        self.savedPath = os.getcwd()
        os.chdir(self.newPath)

    def __exit__(self, etype, value, traceback):
        os.chdir(self.savedPath)

import subprocess, json
def get_j(folder):
	with cd(folder):
		return json.loads(subprocess.check_output(['python', 'parse.py']))


folders = ["acme_air"  ,"express",  "koa", "restify",  "three",  "webtorrent",  "ws", "lokijs", "sails"]

analyses = []
for folder in folders:
	analyses.append(get_j(folder))
print(json.dumps(analyses))

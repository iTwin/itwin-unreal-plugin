#--------------------------------------------------------------------------------------
#
#     $Source: LaunchWithTimeout.py $
#
#  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import subprocess
import argparse
from subprocess import STDOUT, check_call,call,TimeoutExpired
import os
import sys

parser = argparse.ArgumentParser()

parser.add_argument("-r", "--retry", type=int, dest="retries", help="number of retries before failing",default ="3")
parser.add_argument("-t", "--timeout", type=int, dest="timeout", help="timeout in seconds",default ="300")

(options, args) = parser.parse_known_args()
#print("timeout",options.timeout,"retries", options.retries,"command",args,flush=True)

def killsub(proc_pid):
	try:
		import psutil
		process = psutil.Process(proc_pid)
		for proc in process.children(recursive=True):
			# print ("trying to kill sub process with id ",proc.pid,proc,flush=True)
			proc.kill()
	except:
		# print ("no psutils, trying another way to kill",proc_pid,flush=True)
		if hasattr(os, "killpg"):
			import signal
			print(os.killpg(os.getpgid(proc_pid), signal.SIGKILL))
			

	
def CheckCall2():
	if hasattr(os, "setsid"):
		proc = subprocess.Popen(args, preexec_fn=os.setsid)
	else:
		proc = subprocess.Popen(args)
	try:
		proc.communicate(timeout=options.timeout)
		return proc.returncode
	except TimeoutExpired:
		print ("got timeout signal",flush=True)
		killsub(proc.pid)
		#print ("trying to kill main proc ",proc.pid ,flush=True)
		proc.kill()
		proc.communicate()
		raise


for i in range(options.retries - 1):
	try:
		ret = CheckCall2()
		if not ret:
			print ("command succeded",flush=True)
			sys.exit(0)
		else:
			print ("command failed, retries will follow  retcode", ret,flush=True)
	except Exception as ex:
		print ("command tiemout, retries will follow :", ex,flush=True)

try:
	ret = CheckCall2()
	if not ret:
		sys.exit(0)
	else:
		print ("final command failed, retcode", ret,flush=True)
		sys.exit(ret)
except Exception as ex:
	print ("final command tiemout :", ex,flush=True)

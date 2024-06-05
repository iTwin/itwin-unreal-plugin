#--------------------------------------------------------------------------------------
#
#     $Source: RunTests.py $
#
#  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import subprocess
import time
import sys
import datetime
import platform
import os
import traceback
import threading
import json
import shutil
import base64
import fnmatch

# This script is used by the 'eon_add_test' cmake macro.
# Usage: RunTests.py exePath [timeoutInSeconds] [otherOptArgS ...]

def LogWithDate(msg):
	print('%s>%s: %s'%(os.getpid(), datetime.datetime.now().time().strftime('%H:%M:%S:%f')[:-3], msg))
	sys.stdout.flush()  #   Actually show what we just wrote in the buffer ...

useNoAVX = False
useAVX = False
failureOutputLines = []

# Posix only
def CheckAvxNoAvxLinks(libFolder):
	if os.path.isdir(libFolder):
		avxNoAvx = ''
		if useNoAVX:
			avxNoAvx = 'NO_AVX'
		elif useAVX:
			avxNoAvx = 'AVX'
		if avxNoAvx != '':
			needMakeLinks = False
			if platform.system() == 'Darwin' and not os.path.exists(os.path.join(libFolder, avxNoAvx, 'libEngine.dylib')):
				needMakeLinks = True
			if platform.system() == 'Linux' and not os.path.exists(os.path.join(libFolder, avxNoAvx, 'libEngine.so')):
				needMakeLinks = True
			if needMakeLinks:
				LogWithDate('DANGEROUS: creating AVX/NO_AVX links in RunTests.py but multiple instances of this script may be called in parallel! Links should have been created by CMake.')
				subprocess.Popen(['ln -f ' + avxNoAvx + '/* .'], shell=True, cwd=libFolder)

def LogOutput(output):
	for line in iter(output.readline, b''):
		# Try decoding using the current stdout encoding (eg. cp1252) too,
		# otherwise we get weird errors with some characters (eg. \u221a) in JS tests.
		for encoding in ['utf-8', sys.stdout.encoding]:
			try:
				# The exception (if there is one) will actually be raised by LogWithDate
				# (through the print() call), not by line.decode(), so we must keep the call
				# in this "try" block.
				decodedLine = line.decode(encoding).replace('\r', '').replace('\n', '')
				LogWithDate(decodedLine)
				break
			except:
				pass
		else:
			decodedLine = '(failed to log line, base64-encoded line = %s)'%base64.b64encode(line)
			LogWithDate(decodedLine)
		# Look if the output line matches a pattern that indicates a failure.
		# This is used by Omniverse test, where in some cases (eg. the extension does not load)
		# the launched process (kit.exe) returns 0 even though no tests are actually run.
		for pattern in args.get('failurePatterns', []):
			if fnmatch.fnmatchcase(decodedLine.lower(), pattern.lower()):
				failureOutputLines.append((pattern, decodedLine))
	output.close()

def MakeDirs(path):
	if os.path.isdir(path):
		return
	os.makedirs(path)

# Try importing the screenshot utility module, but don't fail if we cannot.
try:
	import PIL.ImageGrab
except:
	LogWithDate('Screenshots will not be available, see exception stack below.\nNote: This will *not* affect the results of the tests.\n%s'%''.join(traceback.format_exception(*sys.exc_info())))

# The script takes a single argument: a json string containing all parameters.
# Remove the extra backslashes that have been added to prevent brace expansion on mac.
# See comment in eon_add_test.cmake for more infos.
args = json.loads(sys.argv[1].replace('\\{', '{'))
# Values stored in dict may be of type "unicode".
# If we use them in the environment, they must be converted to "str".
exePath = str(args['exePath'])
exeArgs = args.get('exeArgs', [])
timeoutInSeconds = args.get('timeout', 10.)
useAVX = args.get('avx', False)
useNoAVX = args.get('no_avx', False)
useCoverage = 'coverageOutDir' in args and platform.system() == 'Windows'

env = os.environ.copy()
# Add environment variables passed in the command-line.
env.update(args['env'])
if 'qtplatform' in args:
	env['QT_QPA_PLATFORM'] = args['qtplatform']

# Force minimum timeout.
# This is a workaround for the tests that are very slow to start on some build machines.
# Also, since tests are now running in parallel and in RelWithDebInfo, we rescale the timeout.
# And when coverage is enabled, we must increase the timeout even more.
originalTimeout = timeoutInSeconds
timeoutInSeconds = (10 if useCoverage else 3)*max(60., timeoutInSeconds)
LogWithDate('Forcing timeout to %ss (supplied timeout was %ss)'%(timeoutInSeconds, originalTimeout))
LogWithDate('TestRunner start, timeout = %ss, exe = \"%s\"'%(timeoutInSeconds, exePath))

if platform.system() == 'Darwin':	# ie. macOS
	# On Mac the libs are in the "lib" subfolder for all products except "Installer"
	# (Setup/Updater/Uninstaller), for which they are in Application like the executables.
	# In that case, we don't need to bother as there is no AVX/NO_AVX build of the Setup.
	CheckAvxNoAvxLinks(os.path.abspath('program/lib'))

if platform.system() == 'Linux':
	CheckAvxNoAvxLinks(os.path.dirname(exePath))

if platform.system() == 'Windows':
	# add bentley lib path for LumenRT exe (note: in the LumenRT product, the path is modified on the fly by the Launcher)
	env["PATH"] = os.path.join(os.path.dirname(exePath).replace('/', '\\'), "Bentley") + ";" + env["PATH"] #add bentley libs path
	if useNoAVX:
		env["PATH"] = os.path.join(os.path.dirname(exePath).replace('/', '\\'), "NO_AVX") + ";" + env["PATH"]
	elif useAVX:
		env["PATH"] = os.path.join(os.path.dirname(exePath).replace('/', '\\'), "AVX") + ";" + env["PATH"]

timeoutDate = datetime.datetime.now()+datetime.timedelta(seconds=timeoutInSeconds)
# Param "close_fds" must be False, otherwise some tests will fail on some Macs
# (eg. test PluginManagerTests, which launches "sh" from the cpp, and fails for an unknown reason if close_fds is True).
# In python2, this param is False by default, but in python3 it is True by default.
if useCoverage:
	outDir = args['coverageOutDir']
	# Clean output dir.
	if os.path.exists(outDir):
		shutil.rmtree(outDir)
	MakeDirs(outDir)
	proc = subprocess.Popen([
			args['coverageToolExe'],
			'--export_type', 'html:%s'%os.path.normpath(outDir).replace('/', '\\'),
			'--export_type', 'binary:%s'%os.path.normpath(os.path.join(outDir, 'report.binary')).replace('/', '\\'),
			# Add the test executable in the covered module list, so that inline function calls are correctly counted.
			'--modules', exePath.replace('/', '\\'),
		]+['--sources=%s'%os.path.normpath(x).replace('/', '\\') for x in args.get('coverageSources', [])]
		+['--modules=%s'%os.path.normpath(x).replace('/', '\\') for x in args.get('coverageModules', [])]
		+[
			'--',
			exePath,
		# Change working directory so that the same file "LastCoverageResults.log" is not written
		# at the same time by multiple executions of the coverage tool.
		] + exeArgs, env=env, cwd=outDir, close_fds=False, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
else:
	proc = subprocess.Popen([exePath] + exeArgs, env=env, close_fds=False, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
# We want to prepend info (PID...) to each output line of the sub process,
# to make the output more readable when the tests are run in parallel on buildbot.
# Functions for reading the output of a sub-process are blocking, which also blocks our timeout mechanism.
# Thus we read and log the output on a separate thread.
procOutputReadThread = threading.Thread(target = LogOutput, args = (proc.stdout,))
procOutputReadThread.daemon = True
procOutputReadThread.start()
while (proc.poll() == None or procOutputReadThread.is_alive()) and datetime.datetime.now() < timeoutDate:
	time.sleep(0.1)
exitCode = proc.poll()
if exitCode == None:
	LogWithDate('Timeout reached, killing test process')
	# Save screenshot, this may help understand why the test reached timeout (message box etc).
	try:
		if 'PIL.ImageGrab' in sys.modules:
			# On Mac, grab() returns an rgba image, which cannot be saved in jpeg.
			# Hence the conversion to rgb.
			im = PIL.ImageGrab.grab().convert('RGB')
			imPath = os.path.join(os.path.expanduser('~'), 'RunTests_Screens', '%s.jpg'%os.path.splitext(os.path.basename(exePath))[0])
			LogWithDate('Saving screenshot: "%s"'%imPath)
			MakeDirs(os.path.dirname(imPath))
			im.save(imPath, 'JPEG')
		else:
			LogWithDate('Cannot save screenshot, the module was not loaded')
	except:
		LogWithDate('Exception when taking screenshot, see stack below.\n%s'%''.join(traceback.format_exception(*sys.exc_info())))
	if platform.system() == 'Windows':
		# Kill process tree (proc.kill() does ont kill subprocesses apparently).
		try:
			subprocess.check_call(['taskkill', '/PID', str(proc.pid), '/T', '/F'])
		except :
			LogWithDate('Exception while trying to kill process, see stack below.\n%s'%''.join(traceback.format_exception(*sys.exc_info())))
	else:
		proc.kill()
	exitCode = 42
	
unixsignals = { 1: "Hangup",
			2: "Interrupt",
			3: "Quit",
			4: "Illegal Instruction",
			5: "Trace/Breakpoint Trap",
			6: "Abort",
			7: "Emulation Trap",
			8: "Arithmetic Exception",
			9: "Killed",
			10: "Bus Error",
			11: "Segmentation Fault",
			12: "Bad System Call",
			13: "Broken Pipe",
			14: "Alarm Clock",
			15: "Terminated",
			16: "User Signal 1",
			17: "User Signal 2",
			18: "Child Status",
			19: "Power Fail/Restart",
			20: "Window Size Change",
			21: "Urgent Socket Condition",
			22: "Socket I/O Possible",
			23: "Stopped (signal)",
			24: "Stopped (user)",
			25: "Continued",
			26: "Stopped (tty input)",
			27: "Stopped (tty output)",
			28: "Virtual Timer Expired",
			29: "Profiling Timer Expired",
			30: "CPU time limit exceeded",
			31: "File size limit exceeded",
			32: "All LWPs blocked",
			33: "Virtual Interprocessor Interrupt for Threads Library",
			34: "Asynchronous I/O",
}
# Log a summary of failures detected in the output.
if len(failureOutputLines) != 0:
	LogWithDate('Failure patterns detected in test output:\n'+'\n'.join(f'{l[0]}: {l[1]}' for l in failureOutputLines))
	exitCode = 43
# neg value are unix signals
if exitCode >= -34 and exitCode < 0 :
	LogWithDate('TestRunner end (exit code : ' + str(exitCode) + ' : ' + unixsignals[-exitCode] + ')')
elif exitCode == 42 :
	LogWithDate('TestRunner end (exit code : 42 : Timeout)')
elif exitCode == 43 :
	LogWithDate('TestRunner end (exit code : 43 : Failure patterns detected)')
else:
	LogWithDate('TestRunner end (exit code : ' + str(exitCode) + ')')

exit(exitCode)

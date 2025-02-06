#--------------------------------------------------------------------------------------
#
#     $Source: FixStdoutForVS.py $
#
#  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import sys
import re
import os

# This script is designed to to be executed in a command pipeline, taking its input from an Unreal tool,
# eg. "RunUAT.bat xxx | python FixStdoutForVS.py".
# It filters the "false positive" errors that would otherwise be considered as real errors by Visual Studio.
# One issue with this method is that the exit code of the Unreal tool is ignored:
# the exit code of the pipeline is the exit code of the last process (ie. this script).
# So we need to somehow get the exit code of the Unreal tool and return it.
# Fortunately, the Unreal tools we use print the exit code in their stdout, so we can retrieve it from there.

errorRegex = re.compile("error", re.IGNORECASE)
exitCodeRegex = re.compile(r"ExitCode=(?P<exitCode>\d+)", re.IGNORECASE)
exitCode = None
for line in sys.stdin:
	# Replace all errors messages (real errors or false positives).
	# Replacing real errors is not an issue, as we rely on the exit code to inform VS
	# about whether the Unreal tool succeeded or failed.
	sys.stdout.write(errorRegex.sub("<error>", line))
	sys.stdout.flush()
	# Try to detect the exit code.
	exitCodeMatch = exitCodeRegex.search(line)
	if exitCodeMatch != None:
		exitCode = int(exitCodeMatch.group("exitCode"))
print(f"{os.path.basename(__file__)}: last exit code found in stdin: {exitCode}")
sys.exit(exitCode or 0)

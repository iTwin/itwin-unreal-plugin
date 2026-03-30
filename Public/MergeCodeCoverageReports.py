#--------------------------------------------------------------------------------------
#
#     $Source: MergeCodeCoverageReports.py $
#
#  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import sys
import json
import subprocess
import os
import shutil
import xml.etree.ElementTree

args = json.loads(sys.argv[1])
outDir = args['outputDir']
# Clean output dir.
if os.path.exists(outDir):
	shutil.rmtree(outDir)
if not os.path.exists(outDir):
	os.makedirs(outDir)
# Merge all input reports.
outXmlPath = os.path.join(outDir, 'report.cobertura.merged.xml')
subprocess.check_call([
		args['coverageToolExe'],
		'--export_type', 'html:%s'%os.path.normpath(outDir).replace('/', '\\'),
		'--export_type', 'cobertura:%s'%os.path.normpath(outXmlPath).replace('/', '\\'),
	]+['--input_coverage=%s'%os.path.normpath(os.path.join(x, 'report.binary')).replace('/', '\\') for x in args['inputDirs']],
	cwd=outDir)
# Log coverage of each module.
# These log strings are then extracted by buidbot from the log files,
# so if you change these strings you will probably need to adapt the regex on the bbot side.
# Also, here we want to ignore the coverage of the test executables themselves
# (see RunTests.py for why the test executables are covered).
# To filter out the test executable modules, we look at the list of input directories,
# which are actually named from the executables.
# For example, if "C:\Build\CodeCoverage\RelWithDebInfo\unit_configmanager" is in the list of
# input directories, then we know that "unit_configmanager" is a test executable so we can ignore it.
inputDirNames = [os.path.basename(x) for x in args['inputDirs']]
for e in xml.etree.ElementTree.parse(outXmlPath).findall('packages/package'):
	moduleName = os.path.splitext(os.path.basename(e.get("name")))[0]
	if moduleName in inputDirNames:
		# This is a test executable, ignore it (see comment above).
		continue
	print('[eoncov] Code coverage for "%s" is %s%%'%(moduleName, int(round(100*float(e.get("line-rate"))))))

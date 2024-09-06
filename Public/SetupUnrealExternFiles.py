#--------------------------------------------------------------------------------------
#
#     $Source: SetupUnrealExternFiles.py $
#
#  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import json
import sys
import pathlib
import os
import platform

addedFiles = []

hackDuplicateByPlatform = ["zconf.h"]


def CreateIntermediateFile(target: pathlib.Path):
	addedFiles.append(target.as_posix())
	if target.is_symlink():
		target.unlink(True)
		print(f'unlink "{target}".')
			
	with open(target,"w") as f:
		f.write(f'''
#pragma once 
#ifdef MACOS 
#include "{target.stem+"_mac"+target.suffix}" 
#elif defined(WIN32)
#include "{target.stem+"_win"+target.suffix}" 
#endif
''')

def CreateSymlink2(target: pathlib.Path, link: pathlib.Path):
	addedFiles.append(link.as_posix())
	# Do not touch the symlink if it already up-to-date.
	# Otherwise it may trigger unnecessary rebuilds.
 	# - For a folder, "up-to-date" means the link exists and points to the correct target.
	# - For a file, "up-to-date" also means that the target was not modified after the link is created.
	#   If the target has been modified after the link was created, we must re-create the link
    #   so that tools that check the modification date (eg. UBT) will correctly see that the file
	#   (ie. the link) has changed and thus properly rebuild what depends on it.
	isUpToDate = link.is_symlink() and os.path.realpath(link.readlink().as_posix().replace('//?/', '')) == os.path.realpath(target.as_posix())
	if target.is_file():
		isUpToDate = isUpToDate and link.lstat().st_mtime >= target.stat().st_mtime
	if isUpToDate:
		#print(f'Up to Date symlink "{link}" -> "{target}".')
		return
	print(f'Create symlink "{link}" -> "{target}".')
	link.unlink(True)
	link.parent.mkdir(parents=True, exist_ok=True)
	link.symlink_to(target, target.is_dir())

def CreateSymlink(target: str, link: str):
	def Resolve(s: str):
		for (k, v) in defines.items():
			s = s.replace(k, v)
		return s
	if any([target.endswith('/'+f) for f in hackDuplicateByPlatform]):
		print(f'Create duplicate by platform for "{link}" -> "{target}".')
		ln = pathlib.Path(Resolve(link))
		CreateIntermediateFile(ln)
		if platform.system() == 'Darwin':
			ln2 = pathlib.Path.joinpath(ln.parent,ln.stem+"_mac"+ln.suffix)
			CreateSymlink2(pathlib.Path(Resolve(target)), ln2)
		elif platform.system() == 'Windows':
			ln2 = pathlib.Path.joinpath(ln.parent,ln.stem+"_win"+ln.suffix)
			CreateSymlink2(pathlib.Path(Resolve(target)), ln2)
	else:
		CreateSymlink2(pathlib.Path(Resolve(target)), pathlib.Path(Resolve(link)))
	

def SetupCesium(src: str, dst:str):
	#print(f'SetupCesium "{src}" -> "{dst}".')
	for subDir in ['Include', 'Lib']:
		for itemPathSrc in pathlib.Path(src, subDir).glob('*'):
			itemPathRel = itemPathSrc.relative_to(src)
			itemPathDst = pathlib.Path(dst, itemPathRel)
			CreateSymlink2(itemPathSrc, itemPathDst)

if os.path.exists(sys.argv[1]):
	f = open(sys.argv[1])
	args = json.load(f)
	
elif sys.argv[1][0] == "'" and sys.argv[1][-1] == "'" :
	args = json.loads(sys.argv[1][1:-1])
else: 
	args = json.loads(sys.argv[1])
# Parse defines.
for a,b in args.items():
	print(a,b)
defines = {}
for define in args["defines"]:
	# Define from the command line is something like "xxx=yyy".
	s = define.split("=")
	defines[s[0]] = s[1]
	# If the define is "linkerFile:myTarget" -> "c:/aa/bb.lib",
	# add another define "linkerFileName:myTarget" -> "bb.lib".
	defines[s[0].replace("File:", "FileName:")] = os.path.basename(s[1])
exec(pathlib.Path(args["commands"]).read_text())

# Remove obsolete links, based on the list that was saved in the previous run.
addedFiles_old = []
if os.path.isfile(args['added']):
	addedFiles_old = json.loads(pathlib.Path(args['added']).read_text())
for addedFile_old in addedFiles_old:
	if (addedFile_old in addedFiles or
		# Be careful to call lexists() (and not exists()) here.
		not os.path.lexists(addedFile_old) or
		# If symlink exists and is valid, was previously added by this script,
		# and is no more added by this script, then should we delete it?
		# Maybe the link has been added by another script, or by cmake?
		# We cannot know, so it is safer to keep the link.
		(os.path.islink(addedFile_old) and os.path.exists(os.readlink(addedFile_old)))):
		continue
	addedTextFiles = any([addedFile_old.endswith('/'+f) for f in hackDuplicateByPlatform])
			
	if not os.path.islink(addedFile_old) and not addedTextFiles:
		print (f'Cannot delete "{addedFile_old}", as it is not a symlink. Deleting it could result in data loss.\nThis is probably a bug in our build scripts.')
		raise Exception(f'Cannot delete "{addedFile_old}", as it is not a symlink. Deleting it could result in data loss.\nThis is probably a bug in our build scripts.')
	if addedTextFiles:
		print(f'Delete obsolete (not symlink) file "{addedFile_old}".')
	else:
		print(f'Delete obsolete symlink "{addedFile_old}".')
		
	os.remove(addedFile_old)
# Save list of all the current links.
pathlib.Path(args['added']).write_text(json.dumps(addedFiles))

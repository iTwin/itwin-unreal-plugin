#--------------------------------------------------------------------------------------
#
#     $Source: GetVcpkgInfos.py $
#
#  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import json, sys, subprocess, pathlib, os, shutil, re

# This script is used by be_get_vcpkg_infos.cmake, see documentation in this file.

# Transforms eg. "curl[non-http, ssl, schannel, sspi]:x64-windows-static-md-release"
# into "curl".
def getShortPackageName(packageName):
	return re.sub(r"\[.*\]|(:.*)", "", packageName)

args = json.loads(sys.argv[1])
if os.path.exists(args["outDir"]):
	shutil.rmtree(args["outDir"])
os.makedirs(args["outDir"])
# Get list of installed packages in json format.
installedPackages = json.loads(subprocess.check_output(f"{args['vcpkgExe']} list --triplet {args['triplet']} --x-install-root {args['installedDir']} --x-json", shell = True))
# The returned dict may use "long" names as keys, eg. "curl:x64-windows-static-md-release",
# so we create our own dict that uses the short names (eg. "curl") as keys.
packageInfos = {}
for packageInfo in installedPackages.values():
	packageInfos[packageInfo["package_name"]] = packageInfo
# Write the list of packages.
(pathlib.Path(args["outDir"])/"PACKAGES").write_text(";".join(packageInfos.keys()))
# Retrieve the (direct) dependencies of each package.
dependInfos = subprocess.check_output(f"{args['vcpkgExe']} depend-info --x-install-root {args['installedDir']} {' '.join(installedPackages.keys())}",
										cwd = args['sourceDir'], stderr = subprocess.STDOUT, text = True, shell = True)
# The command above returned a list of lines like this (one for each package):
# curl[non-http, ssl, schannel, sspi]:x64-windows-static-md-release: vcpkg-cmake, vcpkg-cmake-config, zlib:x64-windows-static-md-release
for packageDependInfo in dependInfos.strip("\n").split("\n"):
	packageName, dependencies = packageDependInfo.split(": ")
	# Skip warnings like "warning: vcpkg-make is only supported on 'native', which does not match x64-windows-unreal-release"
	# which incidently make our parsing fail (ie. let the other possible warnings be treated as error):
	if packageName == "warning" and "is only supported on 'native" in dependencies:
		continue
	packageName = getShortPackageName(packageName)
	packageInfos[packageName]["beDependencies"] = []
	# vcpkg depend-info bug: not using features restrictions from manifest file (vcpkg.json)!
	# As long as cpp-httplib is built with default-features (ie with brotli support), leave this commented out.
	# This is the default in cesium-native's vcpkg.json, but not in cesium-unreal's overlay port of cesium-native...
	# Although for some reason (cache bug??) it seems to build and require it even when building cesium-native
	# dependencies through cesium-unreal's overlay port (with feature 'dependencies-only')... But not when copying
	# the same in our own vcpkg.json?!
	#if packageName == "brotli":
	#	continue
	for dependency in dependencies.split(", ") if dependencies != "" else []:
		packageInfos[packageName]["beDependencies"].append(getShortPackageName(dependency))
# Iterate over each package and write package-specific info files.
for packageName, packageInfo in packageInfos.items():
	# Write direct dependencies.
	if not "beDependencies" in packageInfo:
		raise Exception(f"Error looking for {packageName}'s dependencies!")
	(pathlib.Path(args["outDir"])/f"DEPENDENCIES_{packageName}").write_text(";".join(packageInfo["beDependencies"]))
	# Retrieve the list of header and lib files for this packages.
	# These informations are retrieved from the files "vcpkg/info/.list" which store the names of all installed items (files, directories).
	includeItems = []
	libItems = []
	for item in (pathlib.Path(args["installedDir"])/"vcpkg/info"/f"{packageName}_{packageInfo['version']}_{packageInfo['triplet']}.list").read_text().strip("\n").split("\n"):
		# Look where the item is installed, to deduce if it is a header or a lib item.
		if pathlib.Path(item).is_relative_to(pathlib.Path(args["triplet"])/"include"):
			itemRel = pathlib.Path(item).relative_to(pathlib.Path(args["triplet"])/"include")
			# We only consider items directly under "include" folder,
			# so that we create symlinks to "root" folders instead of symlinks to each file inside these folders.
			# For example, if the package installs this headers files:
			# include
			# |- catch2
			# |  |- catch_all.hpp
			# |  |- catch_approx.hpp
			# |- catch.hpp
			# Then we will only keep "catch2" and "catch.hpp".
			if itemRel.name == str(itemRel):
				includeItems.append(itemRel.as_posix())
		elif pathlib.Path(item).is_relative_to(pathlib.Path(args["triplet"])/"lib"):
			itemRel = pathlib.Path(item).relative_to(pathlib.Path(args["triplet"])/"lib")
			# Only keep files, not folders.
			if (pathlib.Path(args["installedDir"])/item).is_file():
				libItems.append(itemRel.as_posix())
	# Write list of header and lib items.
	(pathlib.Path(args["outDir"])/f"INCLUDES_{packageName}").write_text(";".join(includeItems))
	(pathlib.Path(args["outDir"])/f"LIBS_{packageName}").write_text(";".join(libItems))

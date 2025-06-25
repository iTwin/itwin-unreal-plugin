# itwin-unreal-plugin

## _About projects built with 0.1.15 and older plugin versions_

In 0.1.16, all `ITwinCesium...` classes have been renamed back to `Cesium...`. Pre-0.1.16 projects will still load thanks to Unreal redirects we have set up: these come at a cost on loading time for large projects, so make sure to resave your project at least once with the new version to save the new class names.

## Overview
The iTwin for Unreal plugin enables streaming of iTwins and reality data from the iTwin cloud into Unreal Engine for high-fidelity visualization and consumption. The plugin and its built-in blureprint API allows Unreal Engine developers to create custom applications which can expand upon the capabilities provided by the plugin out of the box. This enables the creation of tailored interactive experiences around iTwins. In addition, we provide the source code of the plugin for developers who would like to compile the plugin themselves.<br>
The streaming technology is based on the open-source [Cesium 3D tiles] (https://github.com/CesiumGS/cesium) and offers great performance even with large datasets.<br>  
This plugin will be regularly updated. We appreciate your feedback to turn this exciting new technology into the leading foundation for advanced visualization of Digital Twins leveraging game engine technology. We encourage you to participate in the plugin's development with your ideas, requests and wishes, and help us shape the future of visualization together. <br>

![Screenshot of an iTwin project running inside Unreal Engine](docs/plugin_screenshot.jpg)

## Supported features

- Real-time 3D rendering, navigation and visualization of iModels with high performance; other iTwin related data will be added in future updates.
- Accessing saved views 
- Exposed API for custom blueprints (for loading reality data, for example)
- 4D animation playback

## Getting started

Please refer to the documentation folder for installation and use of the plugin.

1. [Installing and using the plugin](Documentation/InstallationAndUse/)
2. [Compiling the plugin yourself - for developers](Documentation/ForDevelopers/)
3. [Transitioning projects from 3DFT to the iTwin for Unreal plugin](Documentation/For3DFTUsers/)
4. [Utilizing 4D animation from Synchro](Documentation/Using4DAnimation/)



# How to use 4D animation from Synchro

Starting from build 0.1.11, the plugin includes preliminary support for playing back 4D animations. Since this is an early access version, there are a few steps and limitations to consider.

Current limitations related to 4D schedules:
* The macOS version of the plugin does not support the 4D schedules yet.
* Transformations and animations along 3D paths are not supported yet (even if you uncheck the checkbox in the Replay settings).
* When geometry is loaded by the Cesium subcomponent, either received from the streaming server, or reloaded from cache, the animation is not immediately applied to the visualization. To avoid showing geometry in a state inconsistent with the current Schedule time, the geometry is hidden entirely until its display state is made consistent. This results in transient cubic-shaped holes in the model. On complex models, a few seconds can pass before the visualization is complete again.
* Please do not change the materials used by the _AITwinCesiumTileset_ actor as this would most likely prevent the animation (including visibilities and cutting planes) from being applied.

We recorded [a video on YouTube](https://www.youtube.com/watch?v=lVu1oj7URv4) to get you up and running with 4D animations. To read the written description, please follow along the following process:

1. To be able to access schedules data, the project must have been resynchronized recently: projects not actively used and/or synchronized before _mid-July_ of this year (_mid-August_ for projects hosted on **East US** datacenter) may need to be resynchronized, although thousands of projects have been synchronized on users' behalf to minimize explicit actions from them: check the last synchronization date on Synchro Control.<br> Synchronizing is done either by submitting actual changes to the project, or by changing any of the synchronization settings in the **Edit iModel** panel for the iModel bearing the 4D schedule (actual list of available settings can vary depending on the iModel's history):<br>
![Screenshot of Synchro control's web interface](https://github.com/iTwin/itwin-unreal-plugin/tree/main/docs/Synchro3.jpg)<br>
![Screenshot of Synchro control's web interface](https://github.com/iTwin/itwin-unreal-plugin/tree/main/docs/Synchro4.jpg)<br>
2. After setting up the app Id and creating the iModel actor in your Unreal level, the geometries start appearing, and the schedule data is queried for all received geometries<br>
You can see the running queries in the "Output log" panel.<br>
When the Schedule data has been fully received, a summary is visible in the Output log:<br>
![Screenshot of Unreal Engine's output log](https://github.com/iTwin/itwin-unreal-plugin/tree/main/docs/Synchro5.jpg)<br>
3. Select the iModel in the Outliner, and select its Synchro4DSchedules component in the Details panel: you will see the Replay settings there, where you can:
   * __Jump to beginning__: jump to the start of the schedule (the default is a time far in the future in order to see the completed project)
   * __Jump to end__ of schedule
   * Set the replay speed (as an FTimespan of schedule time per second of replay time), or use __Auto replay speed__ to fit the whole animation in 30 seconds of replay time.
   * You can edit the __Schedule Time__ field manually, to jump to a specific time point.
   * __Play__/__Pause__ the schedule animation
   * __Stop__ will pause the animation but also display all geometries with their original color and opacities, as if there were no schedule at all.<br>
![Screenshot of Unreal Engine's Synchro settings](https://github.com/iTwin/itwin-unreal-plugin/tree/main/docs/Synchro6.jpg)<br>
4. There are also advanced Replay settings to optionally disable coloring, visibilities or cutting planes during the animation, or to 'Mask out non-animated Elements' which may be confusing when looking at the animation.<br>
![Screenshot of further Unreal Engine Synchro settings](https://github.com/iTwin/itwin-unreal-plugin/tree/main/docs/Synchro7.jpg)


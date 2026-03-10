# How to use 4D animation from Synchro

**Important**: do not change the materials used by the _ACesiumTileset_ actor as this would most likely prevent the animation (including visibilities and cutting planes) from being applied.

We recorded [a video on YouTube](https://www.youtube.com/watch?v=lVu1oj7URv4) to get you up and running with 4D animations. To read the written description, please follow along the following process:

1. To be able to access schedules data, the project must have been resynchronized reasonably recently: projects not actively used and/or synchronized before _mid-July_ 2024 (_mid-August_ for projects hosted on **East US** datacenter) may need to be resynchronized, although thousands of projects have been synchronized on users' behalf to minimize explicit actions from them: check the last synchronization date on Synchro Control.<br> Synchronizing is done either by submitting actual changes to the project, or by changing any of the synchronization settings in the **Edit iModel** panel for the iModel bearing the 4D schedule (actual list of available settings can vary depending on the iModel's history):<br>
![Screenshot of Synchro control's web interface](../../docs/Synchro3.jpg)<br>
![Screenshot of Synchro control's web interface](../../docs/Synchro4.jpg)<br>
1. To be able to replay the 4D animation, **you must use the latest changeset** for the iModel. Earlier changesets might work but there is no guarantee they will.
1. After setting up the app Id and creating the iModel actor in your Unreal level, the geometries start appearing, and the schedule data is queried for all received geometries<br>
You can see the running queries in the "Output log" panel.<br>
When the Schedule data has been fully received, a summary is visible in the Output log:<br>
![Screenshot of Unreal Engine's output log](../../docs/Synchro5.jpg)<br>
1. Select the iModel in the Outliner, and select its Synchro4DSchedules component in the Details panel: you will see the Replay settings there, where you can:
   * __Jump to beginning__: jump to the start of the schedule (the default is a time far in the future in order to see the completed project)
   * __Jump to end__ of schedule
   * Set the replay speed (as an FTimespan of schedule time per second of replay time), or use __Auto replay speed__ to fit the whole animation in 30 seconds of replay time.
   * You can edit the __Schedule Time__ field manually, to jump to a specific time point.
   * __Play__/__Pause__ the schedule animation
   * __Stop__ will pause the animation but also display all geometries with their original color and opacities, as if there were no schedule at all.<br>
![Screenshot of Unreal Engine's Synchro settings](../../docs/Synchro6.jpg)<br>
1. There are also advanced Replay settings to optionally disable coloring, visibilities or cutting planes during the animation, or to 'Mask out non-animated Elements' which may be confusing when looking at the animation.<br>
![Screenshot of further Unreal Engine Synchro settings](../../docs/Synchro7.jpg)



#Mike's Hard Lemonade or Lemonade for short - cause it is a lemon mixer rack 


OK this is still unstable - some notes on how to build and prepare are needed.

First off the plugin generates a %appdata% older as alpha audio  for Lemon Mixer Rack.
You need to currently place the console scan app in that folder to have scan work best.
The trifect of plugins are the main lemon mixer rack (lemonade) that is function - however there still may be some issues with scanning some plugins you will need to have
connected apps loaded such as roland cloud manager or universal audio or ilok manager running properly. Verified plugins may still load but some odd javascript or other
popups are showing up as it seems some plugins try to run code when the plugin is loaded and this still appears to be buggy.

Unloading the plugin and possiblity of creashes exist in FL Studio - I havn't narrowed down the reason FL Studio wont let the plugin unload, I think it may relate to using
or attempting to used locked file access such as fl studio plugindb .ini file or something like that, I'm not entirely sure likely need to check debug logs.
In bigwig the plugin worked fine for unloading so it seems isolated to fl studio.

The plugin should still be considered unstable, hopefully the cause of the bugs will be sorted out but not today.

The lemon slice in and out are working 

Overall the plugin has a bit of plugin delay time that needs to be improved still.

Overall the plugin GUI has had a compacted mode added and unused mixer inserts have been vanished overall tightening the view but the scaling GUI overall is still a little too
large for my 1920x1080 but its gotten better.

Plugins can now also be detached from the mixer slots. Ideally I would still like to rescale plugins outside the plugins themselves or use something like bitwigs parameter
reskinning plugins in collapsed mode, but not entirely sure what is possible however overall the overall size of the mixer is still fairly large more thought is needed.

IMHO plugin has improved since the last build but still some major defects, really liking this though.

Todo more of the same from before, must prioritize figuring out what is causing crashes something is buggy just not sure what yet. All other quality of life improvements are secondary to finding that bug fix. 

First off I think this is GPLv3 I'm not aware of any MIT code at this time in the project, it was based of of LEMON Studio's mixer and developed further
Lemon Studio's repo is here https://github.com/ItsAlphaNeon/Lemon-Studio also this fork is inspiring and equally a great source to draw from https://github.com/Dorfhaus/Lemon-Studio-Michael-Lane-Edition. Michael actually implemented a few things I was thinking about 2 days after I mentioned it and I'd still like to get some similar functionality into Lemonade for loading plugins in specific areas like SSL360 and UA CONSOLE for LUNA.

BUILD INFO: Source structure is the Juce structure. Built in Visual Studio 2026  C++20  JUCE 8.0.12 I think plugins will build suprisingly. Its not very demanding. Otherwise really basic built should be clean. Very easy build. https://github.com/juce-framework/JUCE

I'll likely change the name of this mixer rack to Lemonade or something.

This is an adaptation on Lemon Studio's Mixer with quite a bit of additional adjustments. I have also started to add some new functions I like. 
This is still a little buggy, but it is working and has some unique features. It should be considered unstable currently.
Some plugins do not load as they have additional constraints that require escalation to the real host for verification - such as UAD2 plugins UADx seeems to load fine, waves loads fine. I havn't tested all plugins.  What I can say is that some plugins do not load gracefully. I havn't tested save and load of stuff yet. For some reason for instance UJAM plugins crash fl studio atleast crash screen comes up with complaints for memory access however closing it the project is fine and continues with the plugin loaded but its not likeing something with UJAM for instance. I don't know some load gracefully as normal.  UAD2 hard crashes, so its still unstable and I don't fully understand why some plugins have kookie memory addressing processes that crash out or kill FL Studio but I beleive it is because the plugin is ugly in terms of VST3 standard load processes and its accessing weird memory chains.  FRUITY PLUGINS are not yet supported, I honestly will need to figure out what is required to host fruity plugins  if it is even possible if fruity plugins can be loaded outside of mixer, I just don't know. It may be possible but again I have no clue. Research needed to understand the specific load requirements of the fruity plugins not sure what is publically avaialble on that. I think it is may be possible if they load in Lemonade within fl studio but maybe not another daw.  Sicne I don't actually know FL Studio's actual engine or code in how it provides for the plugins, a lot of it is just guesses on what is publically available. This likely will come much later if at all.  Currently the easiest route to similar plugin access is the clones BUT, I think they won't have full functionaly that native fruity plugins do so not as good but better than nothing.

Note I only have also tested in bitwig loads fine. I may test cubase later, probably won't test other daws as I rarely use them.
 

So I don't know which plugins are stable and which plugins are not if they need host authorization they may not work if they require hardware authorization as I think that may go through the daw itself not entirely sure. This more or less functions as a mixer rack that can be 
loaded as an effect.   Interstingly in FL Studio the 16 tracks I think mirror FL Studio's mixer racks so they may be like a adaptor ontop of fl studio's mixer tracks.
Currently only 16 tracks can be added plus the master.  

Sidechains currently are only usable within the plugins sidechain functions itself, it is not as broad as say FL Studio mixer sidechain being everything in a track but rather it is per insert sidechain that sidechains to a given track. Only plugins with sidechain functions use that insert feature currently.

I added a snap in function so plugins load  in a "rack display" area. They can be minimized or maximized. 

The plugin itself is rather large and likely needs to be resized, I need to work that out a bit so that it loads gracefully size wise.

I think the big accomplishment of this version is that the plugins are sort of starting to snap in position in the rack.

There are still a lot of to dos.

TO DO improve the size loading and scaling.  *** Partially addressed more work needed ***

TO DO figure out how to run authorization through a plugin that is a host rather than the primary host.  *** It seems some plugins such as Roland and UA and ILOK PACE still have bugs as they seem to want to authorize plugins as they scan so ILOK plugins can be buggy***

TO DO update the plugin database to mirror the FLP ORGANIZER database to create a common database system that can interact with fl studio's database when it exists to remove duplication of databases.  *** Some progress on the database scanning but still experimenting with this not complete but progress made ***

To DO add a sidechain module that can be enabled that turns non sidechained plugins into a sidechain function enablement. 

TO DO assign individual lemonade mixer tracks to DAW tracks by number????  *** this was DONE FOR 4 groups input and output this could be advanced more but is fine while still testing ***



It is still very buggy but I got a few of the basic goals done,
a basic mixer rack that can load vst3s
a mixer rack that can snap in plugins to load within the rack itself in a dedicated space
minimization and maximization of the plugins.

Sizing it still a little buggy for everything and the GUI can use a little more refinement but I met my intial milestones

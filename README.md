First off I think this is GPLv3 I'm not aware of any MIT code at this time in the project, it was based of of LEMON Studio's mixer and developed further
Lemon Studio's repo is here https://github.com/ItsAlphaNeon/Lemon-Studio

I'll likely change the name of this mixer rack to Lemonade or something.

This is an adaptation on Lemon Studio's Mixer with quite a bit of additional adjustments. I have also started to add some new functions I like. 
This is still a little buggy, but it is working and has some unique features. It should be considered unstable currently.
Some plugins do not load as they have additional constraints that require escalation to the real host for verification - such as UAD plugins. I havn't tested all plugins
So I don't know which plugins are stable and which plugins are not if they need host authorization they may not work, this more or less functions as a mixer rack that can be 
loaded as an effect.   Interstingly in FL Studio the 16 tracks I think mirror FL Studio's mixer racks so they may be like a adaptor ontop of fl studio's mixer tracks.
Currently only 16 tracks can be added plus the master.  

I added a snap in function.   

The plugin itself is rather large and likely needs to be resized, I need to work that out a bit so that it loads gracefully size wise.

I think the big accomplishment of this version is that the plugins are sort of starting to snap in position in the rack.

There are still a lot of to dos.

TO DO improve the size loading and scaling.

TO DO figure out how to run authorization through a plugin that is a host rather than the primary host.

TO DO update the plugin database to mirror the FLP ORGANIZER database to create a common database system that can interact with fl studio's database when it exists to remove duplication of databases.



It is still very buggy but I got a few of the basic goals done,
a basic mixer rack that can load vst3s
a mixer rack that can snap in plugins to load within the rack itself in a dedicated space
minimization and maximization of the plugins.

Sizing it still a little buggy for everything and the GUI can use a little more refinement but I met my intial milestones

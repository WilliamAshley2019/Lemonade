Put your SQLite amalgamation in THIS folder (LemonMixerRack/Source/ThirdParty/sqlite/):

  header:  sqlite3.h   or  sqlite.h
  source:  sqlite3.c / sqlite3.cpp   or   sqlite.c / sqlite.cpp

The code finds the header by itself. The .c/.cpp must be compiled too:
  CMake    : picked up automatically (re-run cmake after copying the files).
  Projucer : right-click the Source group > Add Existing Files, add the header and the .c/.cpp, make sure
             "Compile" is ticked on the .c/.cpp, then save and re-open in Visual Studio.

Without these files the rack still builds and scans; only the FLP Organizer link and lemon_plugins.db are disabled.
(Plugin scanning itself does not use SQLite.)

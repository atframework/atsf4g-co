libsimulator_uv
======

This's a library to provide a standard way to register commands and send some message to server.
It use libuv to do the network job and use linenoise to do the interactive job.

In this library, linenoise and libuv loop will run on the different thread, and linenoise has some 
problem when another thread write some date to stdout or stderr.So we modify some code of linenoise.

Our modified is here: https://github.com/owent-contrib/linenoise
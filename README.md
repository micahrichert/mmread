# mmread for octave

Currently only Debian and Ubuntu based systems are supported.  Though with minor changes to package names, other distributions would probably work.

Only Ubuntu 16.04 has been tested and extremely minimally at that.

To build:
sudo apt-get install build-essential libavcodec-dev libavformat-dev libswscale-dev liboctave-dev

mkoctfile --mex -DMATLAB_MEX_FILE -lavformat -lavcodec -lswscale -lavutil -lstdc++ FFGrab.cpp


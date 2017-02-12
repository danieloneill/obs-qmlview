# obs-qmlview
QML source for OBS

## To use:

First grab the obs-studio (or obs-studio-ftl) source, make that build and work.

In addition to OBS Studio's Qt requirements, this plugin requires QtQuick (duh) and for the demo QtWebengine and the QML components that come with it.

```Due to a persistent bug in QtWebengine which causes it to simply not initialise when not rendered directly to the screen (which isn't the point of this plugin, since we're basically just rendering it to an OBS source layer) the plugin itself initialises the QtWebengine global object on startup. Even if you don't use it. Sorry. This'll be fixed when the QtWebengine guys fix their jazz. If I don't do this, trying to use a WebEngineView QML component will crash OBS.```

In [obs source root]/plugins edit the file CMakeLists.txt and near the bottom add this line:

```add_subdirectory(obs-qmlview)```

And .. yeah, put this checked-out repository in there too.

Now rebuild.

Now install.

Now start OBS.

Now add a "qmlview" source, and in properties setup the width/height and point it to your qml file.  It uses a Qt URL, so, you can prefix it with http:// if it is a remote resource or file:// if it's local.

A "browser.qml" example file is provided. You need to open it in a text editor and toss in your own URL, and maybe set width/height. I don't know, I haven't checked if the plugin resizes the root object to the specified source dimensions automatically.


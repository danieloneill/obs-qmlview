import QtQuick 2.7
import QtWebEngine 1.2

Item {
    id: container
    width: engine.width
    height: engine.height
    focus: true

	Keys.onPressed: {
		console.log("QML Container: "+JSON.stringify(event,null,2));
	}
    Keys.forwardTo: [ browser ]

    WebEngineView {
	id: browser
        anchors.fill: parent
	//focus: true
	backgroundColor: 'transparent'
        url: 'http://google.ca'
	Keys.onPressed: {
		console.log("QML Browser: "+JSON.stringify(event,null,2));
	}
    }

	Component.onCompleted: {
		browser.forceActiveFocus();
	}
}

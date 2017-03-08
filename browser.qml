import QtQuick 2.7
import QtWebEngine 1.2

Item {
	id: container
	width: engine.width()
	height: engine.height()

	Connections {
		target: engine
		onResized: {
			container.width = engine.width();
			container.height = engine.height();
		}
	}

	WebEngineView {
		anchors.fill: parent
		backgroundColor: 'transparent'
		url: 'https://streamjar.tv/overlay/<your name>/<big long hash>/1'
	}
}

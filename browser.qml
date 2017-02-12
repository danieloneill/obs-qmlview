import QtQuick 2.4
import QtWebEngine 1.2

Rectangle {
	id: top
	color: 'transparent'

	WebEngineView {
		anchors.fill: parent
		backgroundColor: 'transparent'
		url: 'https://streamjar.tv/overlay/<your name>/<big long hash>/1'
	}
}

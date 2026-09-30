/* SPDX-License-Identifier: GPL-2.0-or-later */
// Experimental minimal region-to-clipboard overlay. SelectionEditor retains
// the existing input/selection logic and SpectacleCore retains native exports.
import QtQuick
import org.kde.spectacle.private
import org.kde.kquickimageeditor

MouseArea {
    id: root
    anchors.fill: parent
    focus: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    hoverEnabled: true
    cursorShape: Qt.CrossCursor
    readonly property rect viewportRect: Geometry.mapFromPlatformRect(screenToFollow.geometry, screenToFollow.devicePixelRatio)
    readonly property rect selection: SelectionEditor.selection.rect
    readonly property bool selected: !SelectionEditor.selection.empty
    AnnotationViewport {
        anchors.fill: parent
        document: SpectacleCore.annotationDocument
        viewportRect: root.viewportRect
        enabled: false
    }
    Rectangle {
        x: 0; y: 0; width: parent.width
        height: root.selected ? root.selection.y - root.viewportRect.y : parent.height
        color: "#66000000"
    }
    Rectangle {
        visible: root.selected
        x: 0; y: root.selection.y + root.selection.height - root.viewportRect.y
        width: parent.width; height: parent.height - y
        color: "#66000000"
    }
    Rectangle {
        visible: root.selected
        x: 0; y: root.selection.y - root.viewportRect.y
        width: root.selection.x - root.viewportRect.x; height: root.selection.height
        color: "#66000000"
    }
    Rectangle {
        visible: root.selected
        x: root.selection.x + root.selection.width - root.viewportRect.x
        y: root.selection.y - root.viewportRect.y
        width: parent.width - x; height: root.selection.height
        color: "#66000000"
    }
    Rectangle {
        visible: root.selected
        x: root.selection.x - root.viewportRect.x; y: root.selection.y - root.viewportRect.y
        width: root.selection.width; height: root.selection.height
        color: "transparent"; border.color: "white"; border.width: 1
    }
}

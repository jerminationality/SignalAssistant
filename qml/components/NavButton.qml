import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: btn
    text: "?"
    width: 56
    height: 56
    font.pixelSize: 22
    background: Rectangle {
        radius: 16
        color: btn.down ? "#334155" : "#1f2937"
        border.color: "#475569"
    }
}

import QtQuick

QtObject {
    id: test
    property int fails: 0
    Component.onCompleted: {
        const component = Qt.createComponent("DataLabPage.qml")
        if (component.status !== Component.Ready) {
            console.log("FAIL DataLabPage no carga: " + component.errorString())
            Qt.exit(1)
            return
        }
        const page = component.createObject(null)
        if (!page) { console.log("FAIL DataLabPage no instancia"); Qt.exit(1); return }
        if (page.selectedJobId !== "job-1") { console.log("FAIL seleccion inicial"); fails++ }
        if (page.selectedDocumentId !== "doc-1") { console.log("FAIL documento inicial"); fails++ }
        page.destroy()
        console.log(fails === 0 ? "TODO OK" : (fails + " FALLAS"))
        Qt.exit(fails === 0 ? 0 : 1)
    }
}

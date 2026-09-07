import QtQuick
import "NavigationPolicy.js" as NavigationPolicy

QtObject {
    property int fails: 0

    function check(condition, message) {
        console.log((condition ? "  PASS " : "  FAIL ") + message)
        if (!condition) fails++
    }

    Component.onCompleted: {
        const agent = {
            serverOnly: true,
            keepDuringAgentTransition: true,
            keepDuringThinkingRestart: true
        }
        check(!NavigationPolicy.shouldNavigateToLaunch(agent, false, false, true, false),
              "Agente permanece visible durante el hot-swap sin backend")
        check(NavigationPolicy.pageEnabled(agent, false, false, true),
              "Agente permanece habilitado durante el hot-swap")
        check(NavigationPolicy.shouldNavigateToLaunch(agent, false, false, false, false),
              "Agente vuelve a Lanzar ante una detencion real")

        const tasks = { serverOnly: true, agentOnly: true, keepDuringAgentTransition: true }
        check(!NavigationPolicy.shouldNavigateToLaunch(tasks, false, false, true, false),
              "Tasks conserva la vista durante una transicion activa")
        check(NavigationPolicy.shouldNavigateToLaunch(tasks, true, false, false, false),
              "Tasks exige agente cuando no hay transicion")

        console.log(fails === 0 ? "TODO OK" : (fails + " FALLAS"))
        Qt.exit(fails === 0 ? 0 : 1)
    }
}

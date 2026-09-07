.pragma library

function backendRequirementSatisfied(page, backendAvailable, agentStarting) {
    return !page.serverOnly || backendAvailable
            || (page.keepDuringAgentTransition && agentStarting)
}

function pageEnabled(page, backendAvailable, agentRunning, agentStarting) {
    return backendRequirementSatisfied(page, backendAvailable, agentStarting)
            && (!page.agentOnly || agentRunning || agentStarting)
}

function shouldNavigateToLaunch(page, backendAvailable, agentRunning,
                                agentStarting, thinkingRestarting) {
    const preserveThinkingRestart = page.keepDuringThinkingRestart && thinkingRestarting
    const backendMissing = !backendRequirementSatisfied(page, backendAvailable, agentStarting)
    const agentMissing = page.agentOnly && !agentRunning && !agentStarting
    return (backendMissing && !preserveThinkingRestart) || agentMissing
}

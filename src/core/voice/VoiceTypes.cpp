#include "VoiceTypes.h"

#include <QJsonArray>

QJsonObject VoiceConfig::toJson() const
{
    QJsonObject o;
    o["enabled"]        = enabled;
    o["sttProvider"]    = sttProvider;
    o["sttBaseUrl"]     = sttBaseUrl;
    o["sttModel"]       = sttModel;
    o["sttKeyRef"]      = sttKeyRef;
    o["sttLanguage"]    = sttLanguage;
    o["sttMode"]        = sttMode;
    o["sttEndpointPath"] = sttEndpointPath;
    o["sttManagedEngine"] = sttManagedEngine;
    o["sttManagedCommand"] = sttManagedCommand;
    o["sttManagedArgs"] = QJsonArray::fromStringList(sttManagedArgs);
    o["ttsProvider"]    = ttsProvider;
    o["ttsBaseUrl"]     = ttsBaseUrl;
    o["ttsModel"]       = ttsModel;
    o["ttsVoice"]       = ttsVoice;
    o["ttsKeyRef"]      = ttsKeyRef;
    o["ttsFormat"]      = ttsFormat;
    o["ttsMode"]        = ttsMode;
    o["ttsStreamAudio"] = ttsStreamAudio;
    o["ttsPcmSampleRate"] = ttsPcmSampleRate;
    o["ttsPcmChannels"] = ttsPcmChannels;
    o["ttsManagedVoice"]= ttsManagedVoice;
    o["ttsFallbackMode"] = ttsFallbackMode;
    o["ttsManagedCommand"] = ttsManagedCommand;
    o["ttsManagedArgs"] = QJsonArray::fromStringList(ttsManagedArgs);
    o["pocketPythonPath"] = pocketPythonPath;
    o["pocketLanguage"] = pocketLanguage;
    o["pocketVoice"] = pocketVoice;
    o["pocketVoicePath"] = pocketVoicePath;
    o["pocketModelConfig"] = pocketModelConfig;
    o["pocketPort"] = pocketPort;
    o["pocketQuantize"] = pocketQuantize;
    o["pocketAutoEnable"] = pocketAutoEnable;
    o["qwenBinaryPath"] = qwenBinaryPath;
    o["qwenModelDir"] = qwenModelDir;
    o["qwenModelName"] = qwenModelName;
    o["qwenSpeakerEmbedding"] = qwenSpeakerEmbedding;
    o["qwenReferenceWav"] = qwenReferenceWav;
    o["qwenReferenceText"] = qwenReferenceText;
    o["qwenSpeaker"] = qwenSpeaker;
    o["qwenInstruction"] = qwenInstruction;
    o["qwenLanguage"] = qwenLanguage;
    o["qwenThreads"] = qwenThreads;
    o["inflectPythonPath"] = inflectPythonPath;
    o["inflectModelDir"] = inflectModelDir;
    o["inflectProvider"] = inflectProvider;
    o["ttsAutoConfigure"] = ttsAutoConfigure;
    o["turnMode"]       = turnMode;
    o["vadThreshold"]   = vadThreshold;
    o["vadSilenceMs"]   = vadSilenceMs;
    o["vadSegmentMs"]   = vadSegmentMs;
    o["vadActivationLevel"] = vadActivationLevel;
    o["vadAdaptive"]    = vadAdaptive;
    o["smartTurn"]      = smartTurn;
    o["autoListen"]     = autoListen;
    o["bargeIn"]        = bargeIn;
    o["cursorOcr"]      = cursorOcr;
    return o;
}

VoiceConfig VoiceConfig::fromJson(const QJsonObject &o)
{
    VoiceConfig c;
    c.enabled     = o.value("enabled").toBool(c.enabled);
    c.sttProvider = o.value("sttProvider").toString(c.sttProvider);
    c.sttBaseUrl  = o.value("sttBaseUrl").toString(c.sttBaseUrl);
    c.sttModel    = o.value("sttModel").toString(c.sttModel);
    c.sttKeyRef   = o.value("sttKeyRef").toString(c.sttKeyRef);
    c.sttLanguage = o.value("sttLanguage").toString(c.sttLanguage);
    c.sttMode = o.value("sttMode").toString(c.sttMode).trimmed().toLower();
    if (c.sttMode != QLatin1String("stream_process"))
        c.sttMode = QStringLiteral("http_batch");
    c.sttEndpointPath = o.value("sttEndpointPath").toString(c.sttEndpointPath);
    if (c.sttEndpointPath.isEmpty()) c.sttEndpointPath = QStringLiteral("/v1/audio/transcriptions");
    c.sttManagedEngine = o.value("sttManagedEngine").toString(c.sttManagedEngine);
    c.sttManagedCommand = o.value("sttManagedCommand").toString(c.sttManagedCommand);
    if (o.value("sttManagedArgs").isArray()) {
        for (const QJsonValue &value : o.value("sttManagedArgs").toArray())
            if (value.isString()) c.sttManagedArgs.append(value.toString());
    }
    c.ttsProvider = o.value("ttsProvider").toString(c.ttsProvider);
    c.ttsBaseUrl  = o.value("ttsBaseUrl").toString(c.ttsBaseUrl);
    c.ttsModel    = o.value("ttsModel").toString(c.ttsModel);
    c.ttsVoice    = o.value("ttsVoice").toString(c.ttsVoice);
    c.ttsKeyRef   = o.value("ttsKeyRef").toString(c.ttsKeyRef);
    c.ttsFormat   = o.value("ttsFormat").toString(c.ttsFormat);
    c.ttsMode     = o.value("ttsMode").toString(c.ttsMode);
    if (c.ttsMode.isEmpty()) c.ttsMode = QStringLiteral("auto");
    c.ttsStreamAudio = o.value("ttsStreamAudio").toBool(c.ttsStreamAudio);
    c.ttsPcmSampleRate = qBound(8000, o.value("ttsPcmSampleRate").toInt(c.ttsPcmSampleRate), 192000);
    c.ttsPcmChannels = qBound(1, o.value("ttsPcmChannels").toInt(c.ttsPcmChannels), 2);
    c.ttsManagedVoice = o.value("ttsManagedVoice").toString(c.ttsManagedVoice);
    c.ttsFallbackMode = o.value("ttsFallbackMode").toString(c.ttsFallbackMode);
    c.ttsManagedCommand = o.value("ttsManagedCommand").toString(c.ttsManagedCommand);
    if (o.value("ttsManagedArgs").isArray()) {
        for (const QJsonValue &value : o.value("ttsManagedArgs").toArray())
            if (value.isString()) c.ttsManagedArgs.append(value.toString());
    }
    c.pocketPythonPath = o.value("pocketPythonPath").toString(c.pocketPythonPath).trimmed();
    if (c.pocketPythonPath.isEmpty()) c.pocketPythonPath = QStringLiteral("python");
    c.pocketLanguage = o.value("pocketLanguage").toString(c.pocketLanguage).trimmed().toLower();
    if (c.pocketLanguage != QLatin1String("english")
        && c.pocketLanguage != QLatin1String("french")
        && c.pocketLanguage != QLatin1String("german")
        && c.pocketLanguage != QLatin1String("portuguese")
        && c.pocketLanguage != QLatin1String("italian")
        && c.pocketLanguage != QLatin1String("spanish"))
        c.pocketLanguage = QStringLiteral("spanish");
    c.pocketVoice = o.value("pocketVoice").toString(c.pocketVoice).trimmed();
    if (c.pocketVoice.isEmpty()) c.pocketVoice = QStringLiteral("lola");
    c.pocketVoicePath = o.value("pocketVoicePath").toString(c.pocketVoicePath).trimmed();
    c.pocketModelConfig = o.value("pocketModelConfig").toString(c.pocketModelConfig).trimmed();
    c.pocketPort = qBound(1024, o.value("pocketPort").toInt(c.pocketPort), 65535);
    c.pocketQuantize = o.value("pocketQuantize").toBool(c.pocketQuantize);
    c.pocketAutoEnable = o.value("pocketAutoEnable").toBool(c.pocketAutoEnable);
    c.qwenBinaryPath = o.value("qwenBinaryPath").toString();
    c.qwenModelDir = o.value("qwenModelDir").toString();
    c.qwenModelName = o.value("qwenModelName").toString(c.qwenModelName);
    c.qwenSpeakerEmbedding = o.value("qwenSpeakerEmbedding").toString();
    c.qwenReferenceWav = o.value("qwenReferenceWav").toString();
    c.qwenReferenceText = o.value("qwenReferenceText").toString();
    c.qwenSpeaker = o.value("qwenSpeaker").toString();
    c.qwenInstruction = o.value("qwenInstruction").toString();
    c.qwenLanguage = o.value("qwenLanguage").toString(c.qwenLanguage);
    c.qwenThreads = o.value("qwenThreads").toInt(c.qwenThreads);
    c.inflectPythonPath = o.value("inflectPythonPath").toString(c.inflectPythonPath);
    if (c.inflectPythonPath.trimmed().isEmpty()) c.inflectPythonPath = QStringLiteral("python");
    c.inflectModelDir = o.value("inflectModelDir").toString();
    c.inflectProvider = o.value("inflectProvider").toString(c.inflectProvider).toLower();
    if (c.inflectProvider != QLatin1String("cpu")
        && c.inflectProvider != QLatin1String("directml")
        && c.inflectProvider != QLatin1String("cuda"))
        c.inflectProvider = QStringLiteral("cpu");
    c.ttsAutoConfigure = o.value("ttsAutoConfigure").toBool(c.ttsAutoConfigure);
    c.turnMode = o.value("turnMode").toString(c.turnMode).trimmed().toLower();
    // Aceptar "ptt" en configs editados a mano, pero persistir siempre el
    // nombre canónico. Los valores desconocidos conservan el modo histórico.
    if (c.turnMode == QLatin1String("ptt")) c.turnMode = QStringLiteral("push_to_talk");
    if (c.turnMode != QLatin1String("push_to_talk")) c.turnMode = QStringLiteral("vad");
    c.vadThreshold = o.value("vadThreshold").toDouble(c.vadThreshold);
    c.vadSilenceMs = o.value("vadSilenceMs").toInt(c.vadSilenceMs);
    c.vadSegmentMs = o.value("vadSegmentMs").toInt(c.vadSegmentMs);
    c.vadActivationLevel = o.value("vadActivationLevel").toDouble(c.vadActivationLevel);
    c.vadAdaptive = o.value("vadAdaptive").toBool(c.vadAdaptive);
    c.smartTurn   = o.value("smartTurn").toBool(c.smartTurn);
    c.autoListen  = o.value("autoListen").toBool(c.autoListen);
    c.bargeIn     = o.value("bargeIn").toBool(c.bargeIn);
    // Sin la clave queda en false (el default del struct): un config viejo NO
    // estrena la captura de pantalla por actualizar la app.
    c.cursorOcr   = o.value("cursorOcr").toBool(c.cursorOcr);
    return c;
}

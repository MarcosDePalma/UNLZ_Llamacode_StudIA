#pragma once

#include <QObject>

class DataLabTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void schemaValidation();
    void recordNormalizationAndValidation();
    void jobExtractionAndPersistence();
    void modelResponseParsing();
    void routingAndArbitration();
    void headlessFakeLlmExtraction();
    void headlessRetryAndRepair();
    void benchmarkScoring();
};

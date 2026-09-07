#include <QtTest>
#include <QStandardPaths>
#include <QFile>
#include "core/SecretStore.h"

class SecretStoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void diskRoundTrip();
    void envVarTakesPrecedence();
    void removeAndEmpty();
    void notCommittedToProjectRoot();
    void encryptedAtRest();
};

void SecretStoreTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);   // redirige config/data a ubicación de test
    QFile::remove(SecretStore::filePath());      // estado limpio
}

void SecretStoreTests::diskRoundTrip()
{
    QFile::remove(SecretStore::filePath());
    {
        SecretStore s;
        // QtKeychain persiste fuera del archivo de fallback y puede sobrevivir a
        // una corrida abortada; limpiar también el backend real hace el test idempotente.
        s.remove("MY_KEY");
        QVERIFY(!s.has("MY_KEY"));
        s.set("MY_KEY", "sk-abc");
        QVERIFY(s.has("MY_KEY"));
        QCOMPARE(s.resolve("MY_KEY"), QStringLiteral("sk-abc"));
    }
    // persiste entre instancias
    SecretStore s2;
    QCOMPARE(s2.resolve("MY_KEY"), QStringLiteral("sk-abc"));
    s2.remove("MY_KEY");
}

void SecretStoreTests::envVarTakesPrecedence()
{
    SecretStore s;
    s.set("ENV_REF", "from-disk");
    qputenv("ENV_REF", QByteArrayLiteral("from-env"));
    QCOMPARE(s.resolve("ENV_REF"), QStringLiteral("from-env"));   // env gana
    qunsetenv("ENV_REF");
    QCOMPARE(s.resolve("ENV_REF"), QStringLiteral("from-disk"));  // cae al store
    s.remove("ENV_REF");
}

void SecretStoreTests::removeAndEmpty()
{
    SecretStore s;
    s.set("LC_TMP_REF", "v");
    QVERIFY(s.has("LC_TMP_REF"));
    s.set("LC_TMP_REF", "");          // value vacío = borra
    QVERIFY(!s.has("LC_TMP_REF"));
    QVERIFY(s.resolve("").isEmpty());   // ref vacío nunca resuelve
    s.set("", "x");                     // no-op
    QVERIFY(!s.has(""));
}

void SecretStoreTests::notCommittedToProjectRoot()
{
    // El archivo de secretos vive en AppConfig/AppData (test mode), no en el cwd.
    const QString path = SecretStore::filePath();
    QVERIFY(!path.isEmpty());
    QVERIFY(!path.startsWith(QDir::currentPath()));
}

void SecretStoreTests::encryptedAtRest()
{
    QFile::remove(SecretStore::filePath());
    const QString secret = "sk-super-secret-value-12345";
    bool keychain = false;
    {
        SecretStore s;
        s.set("ENC_REF", secret);
        keychain = s.usingKeychain();
    }
    if (keychain) {
        // Backend keyring del SO: el archivo de fallback no debería usarse.
        QFile f(SecretStore::filePath());
        if (f.open(QIODevice::ReadOnly))
            QVERIFY(!f.readAll().contains(secret.toUtf8()));
    } else {
        // Fallback en archivo: NO debe contener el secreto en claro.
        QFile f(SecretStore::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        f.close();
        QVERIFY(!raw.contains(secret.toUtf8()));
#ifdef Q_OS_WIN
        QVERIFY(raw.contains("dpapi:"));      // marcador de blob DPAPI
#endif
    }
    // En cualquier backend, una instancia nueva resuelve el mismo valor.
    SecretStore s2;
    QCOMPARE(s2.resolve("ENC_REF"), secret);
    s2.remove("ENC_REF");   // limpiar (keyring del SO persiste entre corridas)
}

QTEST_MAIN(SecretStoreTests)
#include "test_secret_store.moc"

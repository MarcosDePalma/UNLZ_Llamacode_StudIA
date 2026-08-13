#pragma once
#include <QObject>
#include <QString>

class QProcess;

// Levanta y apaga el servidor de embeddings de StudIA.
//
// Es un segundo llama-server, aparte del de chat, con un modelo chico (bge-m3)
// que convierte texto en vectores. Hasta ahora habia que arrancarlo a mano con
// servidor_embeddings.bat, y al reiniciar la PC la busqueda semantica quedaba
// apagada sin que nada lo dijera.
//
// Va en CPU a proposito: en uso normal solo vectoriza la pregunta del
// estudiante —un texto corto por consulta, ~0,1 s— y asi le deja la placa
// entera al modelo de chat, que la necesita toda. En una GPU de 8 GB los dos
// juntos no entran.
//
// No depende de AppController: StudIA es un modulo aparte y se busca solo el
// binario y el modelo.
class StudiaEmbedServer : public QObject
{
    Q_OBJECT
public:
    explicit StudiaEmbedServer(QObject *parent = nullptr);
    ~StudiaEmbedServer() override;

    // Deja el servidor andando. Es idempotente y no bloquea:
    //  - si ya hay uno escuchando en el puerto (lo levanto el estudiante a
    //    mano, o quedo de una corrida anterior), lo usa y no arranca otro;
    //  - si falta el binario o el modelo, no hace nada y lo explica en motivo().
    void iniciar();
    void detener();

    bool activo() const { return m_activo; }
    // "" mientras todo va bien; si no, por que no se pudo levantar.
    QString motivo() const { return m_motivo; }
    static QString url();
    // Lo levanto este proceso (false si se encontro uno ya andando).
    bool propio() const { return m_proc != nullptr; }

    // Rutas que se buscan. Vacias si no se encuentran.
    static QString rutaModelo();
    static QString rutaBinario();
    // Carpeta de datos de StudIA junto al ejecutable (<appdir>/StudIA).
    static QString carpetaEmpaquetada();

    static constexpr quint16 kPuerto = 8081;

signals:
    // El servidor paso a estar listo para responder.
    void listo();
    void estadoCambiado();

private:
    void comprobarSalud();
    void marcar(bool activo, const QString &motivo);

    QProcess *m_proc = nullptr;
    bool      m_activo = false;
    QString   m_motivo;
    int       m_intentos = 0;
};

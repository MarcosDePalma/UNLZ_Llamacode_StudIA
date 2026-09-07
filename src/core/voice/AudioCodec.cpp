#include "AudioCodec.h"
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <climits>

namespace {
// Escribe un entero little-endian en out.
void putLE(QByteArray &out, quint32 v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}
}

namespace AudioCodec {

QByteArray pcm16ToWav(const QByteArray &pcm, int sampleRate, int channels)
{
    const quint32 byteRate   = quint32(sampleRate) * channels * 2;
    const quint16 blockAlign = quint16(channels * 2);
    const quint32 dataSize   = quint32(pcm.size());

    QByteArray out;
    out.reserve(44 + pcm.size());
    out.append("RIFF");
    putLE(out, 36 + dataSize, 4);
    out.append("WAVE");
    out.append("fmt ");
    putLE(out, 16, 4);              // subchunk1 size (PCM)
    putLE(out, 1, 2);              // audio format = PCM
    putLE(out, quint16(channels), 2);
    putLE(out, quint32(sampleRate), 4);
    putLE(out, byteRate, 4);
    putLE(out, blockAlign, 2);
    putLE(out, 16, 2);            // bits per sample
    out.append("data");
    putLE(out, dataSize, 4);
    out.append(pcm);
    return out;
}

QByteArray wavExtractPcm(const QByteArray &wav)
{
    if (wav.size() < 12 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE")
        return wav;
    int pos = 12;
    while (pos + 8 <= wav.size()) {
        const QByteArray id = wav.mid(pos, 4);
        const quint32 sz = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(wav.constData() + pos + 4));
        pos += 8;
        if (id == "data")
            return wav.mid(pos, int(sz));
        pos += int(sz);
        if (sz & 1) ++pos; // chunks padded a tamaño par
    }
    return {};
}

bool wavPcm16Format(const QByteArray &wav, int *sampleRate, int *channels)
{
    if (wav.size() < 12 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE")
        return false;
    int pos = 12;
    while (pos + 8 <= wav.size()) {
        const QByteArray id = wav.mid(pos, 4);
        const quint32 sz = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(wav.constData() + pos + 4));
        pos += 8;
        if (id == "fmt ") {
            if (sz < 16 || pos + 16 > wav.size()) return false;
            const uchar *p = reinterpret_cast<const uchar *>(wav.constData() + pos);
            const quint16 audioFmt = qFromLittleEndian<quint16>(p);
            const quint16 ch       = qFromLittleEndian<quint16>(p + 2);
            const quint32 rate     = qFromLittleEndian<quint32>(p + 4);
            const quint16 bits     = qFromLittleEndian<quint16>(p + 14);
            if (audioFmt != 1 || bits != 16 || ch == 0 || rate == 0) return false;
            if (sampleRate) *sampleRate = int(rate);
            if (channels)   *channels = int(ch);
            return true;
        }
        pos += int(sz);
        if (sz & 1) ++pos;
    }
    return false;
}

bool wavPcm16DataRange(const QByteArray &wav, int *sampleRate, int *channels,
                       int *dataOffset, quint32 *dataSize)
{
    if (wav.size() < 12 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE")
        return false;
    bool haveFmt = false;
    bool haveData = false;
    int rate = 0;
    int ch = 0;
    int offset = -1;
    quint32 size = 0;
    int pos = 12;
    while (pos + 8 <= wav.size()) {
        const QByteArray id = wav.mid(pos, 4);
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(wav.constData() + pos + 4));
        const int chunkData = pos + 8;
        if (id == "fmt ") {
            if (chunkSize < 16 || chunkData + 16 > wav.size()) return false;
            const uchar *p = reinterpret_cast<const uchar *>(wav.constData() + chunkData);
            const quint16 format = qFromLittleEndian<quint16>(p);
            ch = int(qFromLittleEndian<quint16>(p + 2));
            rate = int(qFromLittleEndian<quint32>(p + 4));
            const quint16 bits = qFromLittleEndian<quint16>(p + 14);
            if (format != 1 || ch <= 0 || rate <= 0 || bits != 16) return false;
            haveFmt = true;
        } else if (id == "data") {
            offset = chunkData;
            size = chunkSize;
            haveData = true;
            break;
        }
        const qint64 next = qint64(chunkData) + qint64(chunkSize)
            + (chunkSize & 1u);
        if (next > wav.size() || next > INT_MAX) return false;
        pos = int(next);
    }
    if (!haveFmt || !haveData || offset < 0) return false;
    if (sampleRate) *sampleRate = rate;
    if (channels) *channels = ch;
    if (dataOffset) *dataOffset = offset;
    if (dataSize) *dataSize = size;
    return true;
}

double rms(const int16_t *samples, int count)
{
    if (count <= 0 || !samples) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double s = double(samples[i]) / 32768.0;
        sum += s * s;
    }
    return std::sqrt(sum / count);
}

double rmsBytes(const QByteArray &pcm)
{
    return rms(reinterpret_cast<const int16_t *>(pcm.constData()),
               int(pcm.size() / 2));
}

} // namespace AudioCodec

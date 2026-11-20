#include "Decoder.h"
#include <QByteArray>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace halo {

namespace {
int interruptCallback(void *opaque)
{
    auto *flag = static_cast<std::atomic_bool *>(opaque);
    return flag && flag->load() ? 1 : 0;
}
QString avError(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return QString::fromUtf8(buf);
}
} // namespace

Decoder::Decoder() = default;
Decoder::~Decoder() { close(); }

bool Decoder::open(const QString &source, QString *error, std::atomic_bool *interrupt)
{
    close();
    m_interrupt = interrupt;
    m_stream = source.startsWith("http://", Qt::CaseInsensitive) || source.startsWith("https://", Qt::CaseInsensitive);
    auto fail = [&](const QString &msg) {
        m_error = msg;
        if (error) *error = msg;
        close();
        return false;
    };

    m_fmt = avformat_alloc_context();
    if (m_interrupt) {
        m_fmt->interrupt_callback.callback = interruptCallback;
        m_fmt->interrupt_callback.opaque = m_interrupt;
    }
    AVDictionary *opts = nullptr;
    if (m_stream) {
        av_dict_set(&opts, "icy", "1", 0);
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "rw_timeout", "15000000", 0);   // 15 s, in microseconds
        av_dict_set(&opts, "user_agent", "HaloSync/0.2", 0);
    }
    const QByteArray url = source.toUtf8();
    int r = avformat_open_input(&m_fmt, url.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0) { m_fmt = nullptr; return fail(avError(r)); }
    if ((r = avformat_find_stream_info(m_fmt, nullptr)) < 0) return fail(avError(r));

    const AVCodec *codec = nullptr;
    m_streamIndex = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m_streamIndex < 0 || !codec) return fail(QStringLiteral("no audio stream"));
    // Album art is a video stream; ignore it.
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i)
        if (int(i) != m_streamIndex) m_fmt->streams[i]->discard = AVDISCARD_ALL;

    AVStream *st = m_fmt->streams[m_streamIndex];
    m_codec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codec, st->codecpar);
    m_codec->pkt_timebase = st->time_base;
    if ((r = avcodec_open2(m_codec, codec, nullptr)) < 0) return fail(avError(r));

    AVChannelLayout out;
    av_channel_layout_default(&out, 2);   // (the AV_CHANNEL_LAYOUT_STEREO literal is C-only)
    AVChannelLayout in;
    if (m_codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || m_codec->ch_layout.nb_channels == 0)
        av_channel_layout_default(&in, std::max(1, m_codec->ch_layout.nb_channels));
    else
        av_channel_layout_copy(&in, &m_codec->ch_layout);
    r = swr_alloc_set_opts2(&m_swr, &out, AV_SAMPLE_FMT_FLT, Rate, &in, m_codec->sample_fmt,
                            m_codec->sample_rate, 0, nullptr);
    m_sourceChannels = in.nb_channels;
    av_channel_layout_uninit(&in);
    // Mono plays at full level on both speakers (FFmpeg's default is -3 dB each).
    if (r >= 0 && m_sourceChannels == 1) {
        const double matrix[2] = {1.0, 1.0};     // out L, out R from the one input channel
        swr_set_matrix(m_swr, matrix, 1);
    }
    if (r < 0 || swr_init(m_swr) < 0) return fail(QStringLiteral("can't set up the resampler"));

    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (m_fmt->duration > 0) m_durationMs = m_fmt->duration / (AV_TIME_BASE / 1000);
    else if (st->duration > 0) m_durationMs = qint64(st->duration * av_q2d(st->time_base) * 1000);
    m_error.clear();
    return true;
}

void Decoder::close()
{
    if (m_frame) av_frame_free(&m_frame);
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_swr) swr_free(&m_swr);
    if (m_codec) avcodec_free_context(&m_codec);
    if (m_fmt) avformat_close_input(&m_fmt);
    m_streamIndex = -1;
    m_eof = m_flushed = false;
    m_durationMs = 0;
    m_skipFrames = 0;
    m_pending.clear();
    m_pendingPos = 0;
}

void Decoder::convert(AVFrame *f)
{
    const int maxOut = swr_get_out_samples(m_swr, f ? f->nb_samples : 0);
    if (maxOut <= 0) return;
    const size_t base = m_pending.size();
    m_pending.resize(base + size_t(maxOut) * Channels);
    uint8_t *dst = reinterpret_cast<uint8_t *>(m_pending.data() + base);
    const int got = swr_convert(m_swr, &dst, maxOut,
                                f ? const_cast<const uint8_t **>(f->extended_data) : nullptr, f ? f->nb_samples : 0);
    m_pending.resize(base + size_t(std::max(0, got)) * Channels);
}

bool Decoder::fill()
{
    if (m_flushed) return false;
    for (;;) {
        int r = avcodec_receive_frame(m_codec, m_frame);
        if (r == 0) {
            m_lastPts = m_frame->best_effort_timestamp;
            convert(m_frame);
            av_frame_unref(m_frame);
            return true;
        }
        if (r == AVERROR_EOF) {
            convert(nullptr);       // drain the resampler
            m_flushed = true;
            return m_pending.size() > m_pendingPos;
        }
        if (r != AVERROR(EAGAIN)) { m_error = avError(r); m_flushed = true; return false; }
        if (m_eof) { avcodec_send_packet(m_codec, nullptr); continue; }

        r = av_read_frame(m_fmt, m_pkt);
        if (r < 0) {
            if (r != AVERROR_EOF && !(m_stream && r == AVERROR_EXIT)) m_error = avError(r);
            m_eof = true;
            avcodec_send_packet(m_codec, nullptr);
            continue;
        }
        if (m_pkt->stream_index == m_streamIndex) {
            r = avcodec_send_packet(m_codec, m_pkt);
            if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_INVALIDDATA) m_error = avError(r);
        }
        av_packet_unref(m_pkt);
    }
}

int Decoder::read(float *out, int frames)
{
    if (!isOpen()) return -1;
    int done = 0;
    while (done < frames) {
        size_t avail = (m_pending.size() - m_pendingPos) / Channels;
        if (avail == 0) {
            m_pending.clear();
            m_pendingPos = 0;
            if (!fill()) break;
            continue;
        }
        if (m_skipFrames > 0) {
            const size_t skip = size_t(std::min<qint64>(m_skipFrames, qint64(avail)));
            m_pendingPos += skip * Channels;
            m_skipFrames -= qint64(skip);
            continue;
        }
        const size_t n = std::min(avail, size_t(frames - done));
        std::memcpy(out + size_t(done) * Channels, m_pending.data() + m_pendingPos, n * Channels * sizeof(float));
        m_pendingPos += n * Channels;
        done += int(n);
    }
    if (done == 0 && !m_error.isEmpty() && m_flushed) return -1;
    return done;
}

bool Decoder::seek(qint64 ms)
{
    if (!isOpen() || m_stream) return false;
    ms = std::max<qint64>(0, ms);
    AVStream *st = m_fmt->streams[m_streamIndex];
    const int64_t ts = av_rescale_q(ms * 1000, AVRational{1, 1000000}, st->time_base);
    const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    if (av_seek_frame(m_fmt, m_streamIndex, start + ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(m_codec);
    swr_init(m_swr);              // drops the resampler's buffered samples
    m_pending.clear();
    m_pendingPos = 0;
    m_eof = m_flushed = false;
    m_error.clear();

    // Find where we landed and drop frames up to the exact target.
    m_skipFrames = 0;
    m_lastPts = AV_NOPTS_VALUE;
    if (!fill()) return true;
    qint64 landedMs = ms;
    if (m_lastPts != AV_NOPTS_VALUE) landedMs = qint64((m_lastPts - start) * av_q2d(st->time_base) * 1000);
    m_skipFrames = std::max<qint64>(0, (ms - landedMs) * Rate / 1000);
    return true;
}

QString Decoder::streamTitle() const
{
    if (!m_fmt || !m_fmt->pb) return {};
    uint8_t *buf = nullptr;
    if (av_opt_get(m_fmt->pb, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &buf) < 0 || !buf) return {};
    const QString packet = QString::fromUtf8(reinterpret_cast<const char *>(buf));
    av_free(buf);
    static const QRegularExpression re(QStringLiteral("StreamTitle='(.*?)';"));
    const auto m = re.match(packet);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

} // namespace halo

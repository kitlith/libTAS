/*
    Copyright 2015-2016 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../logging.h"
#include "AudioContext.h"
#include "AudioPlayer.h"
#include "../global.h" // shared_config

#define MAXBUFFERS 2048 // Max I've seen so far: 960
#define MAXSOURCES 256 // Max I've seen so far: 112

namespace libtas {

AudioContext audiocontext;

/* Helper function to convert ticks into a number of bytes in the audio buffer */
static int ticksToBytes(struct timespec ticks, int alignSize, int frequency)
{
    static int64_t samples_frac = 0;
    uint64_t nsecs = static_cast<uint64_t>(ticks.tv_sec) * 1000000000 + ticks.tv_nsec;
    uint64_t samples = (nsecs * frequency) / 1000000000;
    samples_frac += (nsecs * frequency) % 1000000000;
    if (samples_frac >= 500000000) {
        samples_frac -= 1000000000;
        samples++;
    }
    uint64_t bytes = samples * alignSize;
    return static_cast<int>(bytes);
}

AudioContext::AudioContext(void)
{
    outVolume = 1.0f;
    init();
}

void AudioContext::init(void)
{
    outBitDepth = shared_config.audio_bitdepth;
    outNbChannels = shared_config.audio_channels;
    outFrequency = shared_config.audio_frequency;
    outAlignSize = outNbChannels * outBitDepth / 8;
}

int AudioContext::createBuffer(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (buffers.size() >= MAXBUFFERS)
        return -1;

    /* Check if we can recycle a deleted buffer */
    if (!buffers_pool.empty()) {
        buffers.push_front(buffers_pool.front());
        buffers_pool.pop_front();
        return buffers.front()->id;
    }

    /* If not, we create a new buffer.
     * The next available id equals the size of the buffer list + 1
     * (ids must start by 1, because 0 is reserved for no buffer)
     */
    auto newab = std::make_shared<AudioBuffer>();
    newab->id = buffers.size() + 1;
    buffers.push_front(newab);
    return newab->id;
}

void AudioContext::deleteBuffer(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    buffers.remove_if([id,this](std::shared_ptr<AudioBuffer> const& buffer)
        {
            if (buffer->id == id) {
                /* Push the deleted buffer into the pool */
                buffers_pool.push_front(buffer);
                return true;
            }
            return false;
        });
}

bool AudioContext::isBuffer(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto const& buffer : buffers) {
        if (buffer->id == id)
            return true;
    }

    return false;
}

std::shared_ptr<AudioBuffer> AudioContext::getBuffer(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto& buffer : buffers) {
        if (buffer->id == id)
            return buffer;
    }

    return nullptr;
}

int AudioContext::createSource(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (sources.size() >= MAXSOURCES)
        return -1;

    /* Check if we can recycle a deleted source */
    if (!sources_pool.empty()) {
        sources.push_front(sources_pool.front());
        sources_pool.pop_front();
        return sources.front()->id;
    }

    /* If not, we create a new source.
     * The next available id equals the size of the source list + 1
     * (ids must start by 1, because 0 is reserved for no source)
     */
    auto newas = std::make_shared<AudioSource>();
    newas->id = sources.size() + 1;
    sources.push_front(newas);
    return newas->id;
}

void AudioContext::deleteSource(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    sources.remove_if([id,this](std::shared_ptr<AudioSource> const& source)
        {
            if (source->id == id) {
                /* Push the deleted buffer into the pool */
                sources_pool.push_front(source);
                return true;
            }
            return false;
        });
}

bool AudioContext::isSource(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto& source : sources) {
        if (source->id == id)
            return true;
    }

    return false;
}

std::shared_ptr<AudioSource> AudioContext::getSource(int id)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto& source : sources) {
        if (source->id == id)
            return source;
    }

    return nullptr;
}

void AudioContext::mixAllSources(struct timespec ticks)
{
    /* Check that ticks is positive! */
    if (ticks.tv_sec < 0) {
        debuglog(LCF_SOUND | LCF_FRAME | LCF_ERROR, "Negative number of ticks for audio mixing!");
        return;
    }

    outBytes = ticksToBytes(ticks, outAlignSize, outFrequency);
  	/* Save the actual number of samples and size */
  	outNbSamples = outBytes / outAlignSize;

    debuglog(LCF_SOUND | LCF_FRAME, "Start mixing about ", outNbSamples, " samples");

    /* Silent the output buffer */
    if (outBitDepth == 8) // Unsigned 8-bit samples
        outSamples.assign(outBytes, 0x80);
    if (outBitDepth == 16) // Signed 16-bit samples
        outSamples.assign(outBytes, 0);

    std::lock_guard<std::mutex> lock(mutex);

    for (auto& source : sources) {
        source->mixWith(ticks, &outSamples[0], outBytes, outBitDepth, outNbChannels, outFrequency, outVolume);
    }

#ifdef LIBTAS_ENABLE_SOUNDPLAYBACK
    if (!shared_config.audio_mute) {
        /* Play the music */
        AudioPlayer::play(*this);
    }
#endif
}

}

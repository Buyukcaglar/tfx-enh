#include "output.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>

#define SYNTH_FRAMES 4096

struct tfx_mac_output {
    AudioComponentInstance unit;
    tfx_mac_mixer *mixer;
    uint64_t (*now_us)(void);
    AudioComponentInstance synth; /* the DLS synthesizer, NULL without it */
    Float64 synth_time;
    float left[SYNTH_FRAMES], right[SYNTH_FRAMES];
};

/* the music's synthesizer (music.h), on the output's render thread */
static void synth_midi(void *ctx, const uint8_t *msg, int n, uint32_t offset) {
    tfx_mac_output *o = ctx;
    if (msg[0] == 0xf0)
        MusicDeviceSysEx(o->synth, msg, (UInt32)n);
    else if (msg[0] < 0xf0)
        MusicDeviceMIDIEvent(o->synth, msg[0], n > 1 ? msg[1] : 0, n > 2 ? msg[2] : 0, offset);
}

static void synth_render(void *ctx, float *out, uint32_t frames) {
    tfx_mac_output *o = ctx;
    for (uint32_t done = 0; done < frames;) {
        UInt32 k = frames - done < SYNTH_FRAMES ? frames - done : SYNTH_FRAMES;
        struct {
            AudioBufferList l;
            AudioBuffer second;
        } b = {{2, {{1, k * 4, o->left}}}, {1, k * 4, o->right}};
        AudioTimeStamp ts = {.mSampleTime = o->synth_time, .mFlags = kAudioTimeStampSampleTimeValid};
        AudioUnitRenderActionFlags flags = 0;
        if (AudioUnitRender(o->synth, &flags, &ts, 0, k, &b.l) == noErr)
            for (UInt32 i = 0; i < k; i++) {
                out[2 * (done + i)] += o->left[i];
                out[2 * (done + i) + 1] += o->right[i];
            }
        o->synth_time += k;
        done += k;
    }
}

static AudioComponentInstance open_synth(void) {
    AudioComponentDescription d = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &d);
    AudioComponentInstance u;
    if (!comp || AudioComponentInstanceNew(comp, &u))
        return NULL;
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = TFX_MAC_OUTPUT_RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = f.mBytesPerPacket = 4;
    UInt32 max = SYNTH_FRAMES;
    if (AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f) ||
        AudioUnitSetProperty(u, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max,
                             sizeof max) ||
        AudioUnitInitialize(u)) {
        AudioComponentInstanceDispose(u);
        return NULL;
    }
    return u;
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *stamp, UInt32 bus,
                       UInt32 frames, AudioBufferList *data) {
    (void)flags, (void)stamp, (void)bus;
    tfx_mac_output *o = ref;
    tfx_mac_mixer_render(o->mixer, data->mBuffers[0].mData, frames, o->now_us());
    return noErr;
}

tfx_mac_output *tfx_mac_output_open(tfx_mac_mixer *m, uint64_t (*now_us)(void)) {
    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &d);
    tfx_mac_output *o = calloc(1, sizeof *o);
    if (!comp || !o || AudioComponentInstanceNew(comp, &o->unit)) {
        free(o);
        return NULL;
    }
    o->mixer = m, o->now_us = now_us;
    o->synth = open_synth();
    if (o->synth) {
        pthread_mutex_lock(&m->lock);
        m->music.synth = (tfx_mac_synth){o, synth_midi, synth_render};
        pthread_mutex_unlock(&m->lock);
    }
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = TFX_MAC_OUTPUT_RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = f.mBytesPerPacket = 8;
    AURenderCallbackStruct cb = {render, o};
    if (AudioUnitSetProperty(o->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) ||
        AudioUnitSetProperty(o->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                             sizeof cb) ||
        AudioUnitInitialize(o->unit) || AudioOutputUnitStart(o->unit)) {
        tfx_mac_output_close(o);
        return NULL;
    }
    return o;
}

int tfx_mac_output_synth(const tfx_mac_output *o) { return o && o->synth; }

void tfx_mac_output_close(tfx_mac_output *o) {
    if (!o)
        return;
    AudioOutputUnitStop(o->unit);
    AudioUnitUninitialize(o->unit);
    AudioComponentInstanceDispose(o->unit);
    if (o->synth) {
        pthread_mutex_lock(&o->mixer->lock);
        o->mixer->music.synth = (tfx_mac_synth){0};
        pthread_mutex_unlock(&o->mixer->lock);
        AudioUnitUninitialize(o->synth);
        AudioComponentInstanceDispose(o->synth);
    }
    free(o);
}

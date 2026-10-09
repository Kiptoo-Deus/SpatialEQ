// JNI bridge between Kotlin (com.savannahdsp.spatialeq.audio.NativeEngine) and the shared DSP core.
#include <jni.h>

#include <algorithm>

#include "spatialeq_dsp.h"
#include "spatialeq_spectrum.h"

namespace {

// Parameter block packing, mirrored in SoundSettings.pack() (Kotlin):
//   [0] enabled [1] preampDb [2] eqEnabled [3] numBands
//   [4 .. 4+16*5)  bands: enabled, type, freq, gainDb, q
//   [84] mode [85] spatialEnabled [86] upmix
//   [87 .. 87+8*4) sources: azimuth, elevation, distance, gain
//   [119] width [120] xtcEnabled [121] xtcStrength [122] speakerSpan [123] roomSize [124] reverbMix
//   [125] bassBoostDb [126] dialogueBoost [127] levelerEnabled [128] levelerTargetDb
//   [129] limiterEnabled [130] limiterCeilingDb [131] outputGainDb
constexpr int kPackedSize = 132;

void unpack(const float* a, sq_params* p) {
    sq_params_default(p);
    p->enabled = a[0] != 0;
    p->preampDb = a[1];
    p->eqEnabled = a[2] != 0;
    p->numBands = std::clamp(int(a[3]), 0, SQ_MAX_BANDS);
    for (int i = 0; i < SQ_MAX_BANDS; ++i) {
        const float* b = a + 4 + i * 5;
        p->bands[i] = {b[0] != 0, int(b[1]), b[2], b[3], b[4]};
    }
    p->mode = int(a[84]);
    p->spatialEnabled = a[85] != 0;
    p->upmix = int(a[86]);
    for (int i = 0; i < SQ_MAX_SOURCES; ++i) {
        const float* s = a + 87 + i * 4;
        p->sources[i] = {s[0], s[1], s[2], s[3]};
    }
    p->width = a[119];
    p->xtcEnabled = a[120] != 0;
    p->xtcStrength = a[121];
    p->speakerSpanDeg = a[122];
    p->roomSize = a[123];
    p->reverbMix = a[124];
    p->bassBoostDb = a[125];
    p->dialogueBoost = a[126];
    p->levelerEnabled = a[127] != 0;
    p->levelerTargetDb = a[128];
    p->limiterEnabled = a[129] != 0;
    p->limiterCeilingDb = a[130];
    p->outputGainDb = a[131];
}

struct Native {
    sq_engine* engine = sq_engine_create();
    sq_spectrum* spectrum = nullptr;
    double sampleRate = 48000;
};

Native* N(jlong h) { return reinterpret_cast<Native*>(h); }

bool readParams(JNIEnv* env, jfloatArray arr, sq_params* p) {
    if (!arr || env->GetArrayLength(arr) < kPackedSize) return false;
    float packed[kPackedSize];
    env->GetFloatArrayRegion(arr, 0, kPackedSize, packed);
    unpack(packed, p);
    return true;
}

} // namespace

#define JNI_FN(name) Java_com_savannahdsp_spatialeq_audio_NativeEngine_##name

extern "C" {

JNIEXPORT jlong JNICALL JNI_FN(nativeCreate)(JNIEnv*, jclass) { return reinterpret_cast<jlong>(new Native()); }

JNIEXPORT void JNICALL JNI_FN(nativeDestroy)(JNIEnv*, jclass, jlong h) {
    auto* n = N(h);
    if (n->spectrum) sq_spectrum_destroy(n->spectrum);
    sq_engine_destroy(n->engine);
    delete n;
}

JNIEXPORT void JNICALL JNI_FN(nativePrepare)(JNIEnv*, jclass, jlong h, jint sampleRate, jint maxFrames) {
    N(h)->sampleRate = sampleRate;
    sq_engine_prepare(N(h)->engine, sampleRate, maxFrames);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetParams)(JNIEnv* env, jclass, jlong h, jfloatArray packed) {
    sq_params p;
    if (readParams(env, packed, &p)) sq_engine_set_params(N(h)->engine, &p);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetHeadYaw)(JNIEnv*, jclass, jlong h, jfloat yaw) {
    sq_engine_set_head_yaw(N(h)->engine, yaw);
}

// Processes interleaved stereo float audio in place. Called from the capture thread only.
JNIEXPORT void JNICALL JNI_FN(nativeProcess)(JNIEnv* env, jclass, jlong h, jfloatArray buffer, jint frames) {
    auto* data = static_cast<float*>(env->GetPrimitiveArrayCritical(buffer, nullptr));
    if (!data) return;
    sq_engine_process_interleaved(N(h)->engine, data, 2, data, 2, frames);
    env->ReleasePrimitiveArrayCritical(buffer, data, 0);
}

// Fills `bands` with the live spectrum (0..1); returns the overall level.
JNIEXPORT jfloat JNICALL JNI_FN(nativeSpectrum)(JNIEnv* env, jclass, jlong h, jfloatArray bands) {
    auto* n = N(h);
    const int count = env->GetArrayLength(bands);
    if (!n->spectrum) n->spectrum = sq_spectrum_create(count);
    float tmp[256];
    const int c = std::min(count, 256);
    const float level = sq_spectrum_update(n->spectrum, n->engine, n->sampleRate, tmp);
    env->SetFloatArrayRegion(bands, 0, c, tmp);
    return level;
}

JNIEXPORT void JNICALL JNI_FN(nativeMeters)(JNIEnv* env, jclass, jlong h, jfloatArray out) {
    sq_meters m;
    sq_engine_get_meters(N(h)->engine, &m);
    const float v[4] = {m.peakL, m.peakR, m.limiterReductionDb, m.levelerGainDb};
    env->SetFloatArrayRegion(out, 0, 4, v);
}

JNIEXPORT void JNICALL JNI_FN(nativeEqResponse)(JNIEnv* env, jclass, jfloatArray packed, jfloat sampleRate,
                                                jfloatArray freqs, jfloatArray outDb) {
    sq_params p;
    if (!readParams(env, packed, &p)) return;
    const int n = std::min(env->GetArrayLength(freqs), 512);
    float f[512], db[512];
    env->GetFloatArrayRegion(freqs, 0, n, f);
    sq_eq_response(&p, sampleRate, f, db, n);
    env->SetFloatArrayRegion(outDb, 0, n, db);
}

} // extern "C"
